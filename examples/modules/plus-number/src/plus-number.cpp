// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "plus-number.h"

using namespace napa::demo;

PlusNumber::PlusNumber(double value)
    : _value(value) {
}

double PlusNumber::Add(double value) {
    return _value + value;
}
