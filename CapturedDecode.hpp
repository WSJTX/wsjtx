// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CAPTURED_DECODE_HPP
#define CAPTURED_DECODE_HPP

#include "DecoderParams.h"

// Audio and settings taken now for a decode submitted later to the backend
// that captured them.
class CapturedDecode
{
public:
  virtual ~CapturedDecode () = default;
  virtual decoder_params_t const& settings () const = 0;
};

#endif
