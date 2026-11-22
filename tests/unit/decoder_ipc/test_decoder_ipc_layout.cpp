#include <QtTest>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "DecoderIpc.hpp"
#include "lib/decoder_ipc_layout.h"

class TestDecoderIpcLayout final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void compiledLayoutsAgree ();
  void everyDescriptorWordIsRequired ();
  void equalSizeParameterSwapIsRejected ();
  void malformedStorageIsRejected ();
  void truncatedStorageIsRejectedBeforePayloadAccess ();
  void allocationPaddingIsAccepted ();
  void publicationAndClaimRejectIncompatibleLayout ();
};

void TestDecoderIpcLayout::compiledLayoutsAgree ()
{
  decoder_ipc_layout_t cpp {};
  decoder_ipc_layout_t fortran {};
  decoder_ipc_expected_layout (&cpp);
  decoder_ipc_fortran_layout (&fortran);
  QCOMPARE (std::memcmp (&cpp, &fortran, sizeof cpp), 0);
  QCOMPARE (cpp.field_count, DECODER_IPC_LAYOUT_FIELDS);
  QCOMPARE (cpp.payload_offset, static_cast<int> (offsetof (shared_dec_data_t, payload)));
  QCOMPARE (cpp.payload_bytes, static_cast<int> (sizeof (dec_data_t)));
  QCOMPARE (cpp.logical_bytes, static_cast<int> (sizeof (shared_dec_data_t)));

  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  DecoderIpc::initialize (*shared);
  QCOMPARE (decoder_ipc_validate_layout (shared.get (), sizeof *shared, &fortran),
            int {DECODER_IPC_LAYOUT_OK});
  QCOMPARE (decoder_ipc_fortran_validate (shared.get (), sizeof *shared),
            int {DECODER_IPC_LAYOUT_OK});
}

void TestDecoderIpcLayout::everyDescriptorWordIsRequired ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  DecoderIpc::initialize (*shared);
  auto const valid = shared->layout;
  std::array<int, sizeof (decoder_ipc_layout_t) / sizeof (int)> words;
  std::memcpy (words.data (), &valid, sizeof valid);

  for (std::size_t index = 0; index < words.size (); ++index)
    {
      words[index] ^= 1;
      std::memcpy (&shared->layout, words.data (), sizeof valid);
      auto const before = shared->layout;
      auto const message = QByteArray::number (static_cast<qulonglong> (index));
      QVERIFY2 (decoder_ipc_validate_layout (shared.get (), sizeof *shared, nullptr)
                != DECODER_IPC_LAYOUT_OK, message.constData ());
      QVERIFY2 (decoder_ipc_fortran_validate (shared.get (), sizeof *shared)
                != DECODER_IPC_LAYOUT_OK, message.constData ());
      QCOMPARE (std::memcmp (&before, &shared->layout, sizeof before), 0);
      shared->layout = valid;
      QCOMPARE (decoder_ipc_validate_layout (shared.get (), sizeof *shared, &before),
                int {DECODER_IPC_LAYOUT_MISMATCH});
      words[index] ^= 1;
    }
}

void TestDecoderIpcLayout::equalSizeParameterSwapIsRejected ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  DecoderIpc::initialize (*shared);
  auto& first = shared->layout.fields[9]; // params.nutc
  auto& second = shared->layout.fields[11]; // params.ntrperiod
  QCOMPARE (first.bytes, second.bytes);
  std::swap (first.offset, second.offset);
  QCOMPARE (decoder_ipc_fortran_validate (shared.get (), sizeof *shared),
            int {DECODER_IPC_LAYOUT_MISMATCH});
}

void TestDecoderIpcLayout::malformedStorageIsRejected ()
{
  QCOMPARE (decoder_ipc_validate_layout (nullptr, sizeof (shared_dec_data_t), nullptr),
            int {DECODER_IPC_LAYOUT_NULL});
  QCOMPARE (decoder_ipc_fortran_validate (nullptr, sizeof (shared_dec_data_t)),
            int {DECODER_IPC_LAYOUT_NULL});

  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  DecoderIpc::initialize (*shared);
  auto const* unaligned = reinterpret_cast<unsigned char const *> (shared.get ()) + 1;
  QCOMPARE (decoder_ipc_validate_layout (unaligned, sizeof *shared - 1, nullptr),
            int {DECODER_IPC_LAYOUT_ALIGNMENT});
  QCOMPARE (decoder_ipc_fortran_validate (unaligned, sizeof *shared - 1),
            int {DECODER_IPC_LAYOUT_ALIGNMENT});

  for (auto const version : {0, 1, 2, 3, 4, DECODER_IPC_VERSION + 1})
    {
      shared->control.version = version;
      QCOMPARE (decoder_ipc_fortran_validate (shared.get (), sizeof *shared),
                int {DECODER_IPC_LAYOUT_VERSION});
      QCOMPARE (shared->control.version, version);
    }
  shared->control.version = DECODER_IPC_VERSION;
  shared->control.state = -1;
  QCOMPARE (decoder_ipc_fortran_validate (shared.get (), sizeof *shared),
            int {DECODER_IPC_LAYOUT_STATE});
  QCOMPARE (shared->control.state, -1);
  shared->control.state = DECODER_IPC_IDLE;

  for (auto const capabilities : {0, DECODER_IPC_CAPABILITIES & ~DECODER_IPC_CAP_HANDSHAKE,
                                  DECODER_IPC_CAPABILITIES | 8})
    {
      shared->layout.capabilities = capabilities;
      QCOMPARE (decoder_ipc_fortran_validate (shared.get (), sizeof *shared),
                int {DECODER_IPC_LAYOUT_CAPABILITIES});
      QCOMPARE (shared->layout.capabilities, capabilities);
    }
}

void TestDecoderIpcLayout::truncatedStorageIsRejectedBeforePayloadAccess ()
{
  alignas(shared_dec_data_t) std::array<unsigned char,
      offsetof (shared_dec_data_t, payload)> header {};
  decoder_ipc_control_t const control {0, DECODER_IPC_IDLE, DECODER_IPC_VERSION, 0};
  decoder_ipc_layout_t layout;
  decoder_ipc_expected_layout (&layout);
  std::memcpy (header.data (), &control, sizeof control);
  std::memcpy (header.data () + offsetof (shared_dec_data_t, layout), &layout, sizeof layout);
  auto const before = header;
  for (auto const size : {std::size_t {0}, sizeof control - 1,
                         sizeof control, header.size () - 1, header.size ()})
    {
      QCOMPARE (decoder_ipc_validate_layout (header.data (), size, nullptr),
                int {DECODER_IPC_LAYOUT_TRUNCATED});
      QCOMPARE (decoder_ipc_fortran_validate (header.data (), size),
                int {DECODER_IPC_LAYOUT_TRUNCATED});
      QVERIFY (header == before);
    }

  layout.header_bytes = -1;
  std::memcpy (header.data () + offsetof (shared_dec_data_t, layout), &layout, sizeof layout);
  QCOMPARE (decoder_ipc_fortran_validate (header.data (), header.size ()),
            int {DECODER_IPC_LAYOUT_HEADER_SIZE});
  decoder_ipc_expected_layout (&layout);
  layout.payload_bytes = -1;
  std::memcpy (header.data () + offsetof (shared_dec_data_t, layout), &layout, sizeof layout);
  QCOMPARE (decoder_ipc_fortran_validate (header.data (), header.size ()),
            int {DECODER_IPC_LAYOUT_PAYLOAD_SIZE});
}

void TestDecoderIpcLayout::allocationPaddingIsAccepted ()
{
  std::vector<int> storage ((sizeof (shared_dec_data_t) + 4096) / sizeof (int));
  auto* shared = new (storage.data ()) shared_dec_data_t;
  DecoderIpc::initialize (*shared);
  QCOMPARE (decoder_ipc_fortran_validate (shared, storage.size () * sizeof (int)),
            int {DECODER_IPC_LAYOUT_OK});
  shared->layout.logical_bytes += 4;
  QCOMPARE (decoder_ipc_fortran_validate (shared, storage.size () * sizeof (int)),
            int {DECODER_IPC_LAYOUT_LOGICAL_SIZE});
  shared->~shared_dec_data_t ();
}

void TestDecoderIpcLayout::publicationAndClaimRejectIncompatibleLayout ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  std::unique_ptr<dec_data_t> payload {new dec_data_t {}};
  DecoderIpc::initialize (*shared);
  shared->payload.d2[0] = 42;
  payload->d2[0] = 17;
  payload->params.nmode = 8;
  shared->layout.payload_bytes -= 4;
  QByteArray before (reinterpret_cast<char const *> (shared.get ()), sizeof *shared);
  QVERIFY (!DecoderIpc::publish (*shared, *payload, true, 1));
  QCOMPARE (std::memcmp (shared.get (), before.constData (), sizeof *shared), 0);

  shared->control.state = DECODER_IPC_READY;
  shared->control.generation = 1;
  before = QByteArray (reinterpret_cast<char const *> (shared.get ()), sizeof *shared);
  qint32 generation {99};
  QVERIFY (!DecoderIpc::claim (*shared, generation));
  QCOMPARE (generation, qint32 {99});
  QCOMPARE (std::memcmp (shared.get (), before.constData (), sizeof *shared), 0);
}

QTEST_GUILESS_MAIN (TestDecoderIpcLayout)

#include "test_decoder_ipc_layout.moc"
