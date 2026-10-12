#!/usr/bin/env python3
"""Exercise patched FUNcube reads with a mock USB transport."""

import argparse
import pathlib
import re
import shlex
import subprocess
import tempfile


FUNCTIONS = (
    "funcube_read_report", "set_freq_v0", "set_freq_v1", "funcube_set_freq",
    "get_freq_v0", "get_freq_v1", "funcube_get_freq", "funcube_hid_cmd",
)

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "funcube.h"

#if FUNCUBE_TEST_WINDOWS
#ifndef _WIN32
#define _WIN32
#endif
#else
#undef _WIN32
#endif

#define LIBUSB_ERROR_IO -1
#define LIBUSB_ERROR_INVALID_PARAM -2
#define RIG_OK 0
#define RIG_EIO 6
#define rig_debug(...) ((void)0)
typedef double freq_t;
typedef int vfo_t;
typedef struct { int unused; } libusb_device_handle;
struct funcube_priv_data { freq_t freq; };
typedef struct { void *handle; int timeout; } hamlib_port_t;
typedef struct {
    struct { void *priv; } state;
    hamlib_port_t port;
} RIG;
#define STATE(rig) (&(rig)->state)
#define RIGPORT(rig) (&(rig)->port)

static int reply_length = 64;
static int read_result;
static int legacy_writes;
static int reads;
static unsigned char command;
static uint32_t device_frequency;

static void require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static int libusb_interrupt_transfer(libusb_device_handle *device,
                                    unsigned char endpoint, unsigned char *data,
                                    int length, int *actual_length, int timeout)
{
    (void)device;
    (void)timeout;
    if (endpoint == OUTPUT_ENDPOINT) {
        command = data[0];
        if (command == REQUEST_SET_FREQ_HZ) {
            device_frequency = (uint32_t)data[1] | ((uint32_t)data[2] << 8)
                | ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
        } else if (command == REQUEST_SET_FREQ) {
            legacy_writes++;
        }
        *actual_length = length;
        return 0;
    }
    require(endpoint == INPUT_ENDPOINT, "Unexpected USB endpoint");
    require(length == 64, "The USB request must remain 64 bytes");
    reads++;
    unsigned char response[65] = { 0 };
    response[0] = command;
    response[1] = FUNCUBE_SUCCESS;
    if (command == REQUEST_GET_FREQ_HZ) {
        for (int i = 0; i < 4; i++)
            response[2 + i] = (unsigned char)(device_frequency >> (8 * i));
    }
    int copied = reply_length < 65 ? reply_length : 65;
    memcpy(data, response, (size_t)copied);
    *actual_length = reply_length;
    return read_result;
}

@FUNCTIONS@

static void check_frequency_roundtrip(RIG *rig, int length)
{
    freq_t frequency = 0;
    reply_length = length;
    legacy_writes = 0;
    require(funcube_set_freq(rig, 0, 14074000) == RIG_OK,
            "A successful Hz command must be accepted");
    require(legacy_writes == 0, "A successful Hz command must not trigger fallback");
    require(funcube_get_freq(rig, 0, &frequency) == RIG_OK,
            "Frequency readback must succeed");
    require(frequency == 14074000, "Frequency readback must remain in Hz");
    require(set_freq_v0(NULL, 14074000, 1000) == RIG_OK,
            "Legacy frequency replies must use the protected reader");
    unsigned char output[64] = { REQUEST_GET_IF_GAIN };
    unsigned char input[64] = { 0 };
    require(funcube_hid_cmd(rig, output, input, sizeof(input)) == RIG_OK,
            "Level replies must use the protected reader");
}

int main(void)
{
    struct funcube_priv_data private_data = { 0 };
    RIG rig = { { &private_data }, { NULL, 1000 } };
    check_frequency_roundtrip(&rig, 64);
#ifdef _WIN32
    check_frequency_roundtrip(&rig, 65);
#endif

    struct { unsigned char data[64]; unsigned char guard; } destination;
    int actual_length;
    for (int length = 64; length <= 65; length++) {
        memset(&destination, 0xa5, sizeof(destination));
        reply_length = length;
        int ret = funcube_read_report(NULL, destination.data, 64, &actual_length, 1000);
#ifdef _WIN32
        require(ret == 0 && actual_length == 64, "Windows length normalization failed");
#else
        require(length == 64 ? ret == 0 : ret == LIBUSB_ERROR_IO,
                "Other platforms must not normalize extra bytes");
#endif
        require(destination.guard == 0xa5, "The caller's buffer was overwritten");
    }

    const int rejected_lengths[] = { 0, 1, 2, 6, 63, 66 };
    for (size_t i = 0; i < sizeof(rejected_lengths) / sizeof(rejected_lengths[0]); i++) {
        reply_length = rejected_lengths[i];
        memset(&destination, 0xa5, sizeof(destination));
        require(funcube_read_report(NULL, destination.data, 64, &actual_length, 1000)
                == LIBUSB_ERROR_IO, "Malformed response length was accepted");
        require(destination.data[0] == 0xa5 && destination.guard == 0xa5,
                "A malformed response was copied to the caller");
        freq_t frequency = -1;
        require(get_freq_v1(&rig, 0, &frequency) == -RIG_EIO && frequency == -1,
                "A malformed response was published as a frequency");
    }

    reply_length = 65;
    read_result = -7;
    require(funcube_read_report(NULL, destination.data, 64, &actual_length, 1000) == -7,
            "USB errors must remain errors");
    require(actual_length == 65, "Error responses must not be normalized");
    int previous_reads = reads;
    require(funcube_read_report(NULL, destination.data, 65, &actual_length, 1000)
            == LIBUSB_ERROR_INVALID_PARAM, "An oversized request was accepted");
    require(reads == previous_reads, "An oversized request reached USB");
    puts("FUNcube reply and frequency checks passed");
    return 0;
}
"""


def extract_function(source, name):
    match = re.search(rf"^(?:static )?int {name}\([^;]*?\)\n\{{", source, re.M)
    if match is None:
        raise ValueError(f"Function definition missing: {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path, help="Patched Hamlib source directory")
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = (args.source / "rigs/kit/funcube.c").read_text()
    if re.search(r"libusb_interrupt_transfer\s*\(\s*udh\s*,\s*INPUT_ENDPOINT\s*,\s*au8BufIn", source):
        raise ValueError("A FUNcube reply read still bypasses the protected reader")
    functions = "\n\n".join(extract_function(source, name) for name in FUNCTIONS)
    with tempfile.TemporaryDirectory(prefix="hamlib-funcube-test-") as temp:
        directory = pathlib.Path(temp)
        fixture = directory / "funcube_test.c"
        fixture.write_text(HARNESS.replace("@FUNCTIONS@", functions))
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-Wno-sign-compare",
                 "-I", str((args.source / "rigs/kit").resolve())]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for platform in ("windows", "other"):
            binary = directory / f"funcube_test_{platform}.exe"
            platform_flag = f"-DFUNCUBE_TEST_WINDOWS={int(platform == 'windows')}"
            subprocess.run(shlex.split(args.compiler) + flags
                           + [platform_flag, str(fixture), "-o", str(binary)], check=True)
            print(f"Checking {platform} reply handling", flush=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
