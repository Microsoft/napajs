// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "serialized-data.h"

#include <napa/module/binding/wraps.h>

namespace napa {
namespace transport {

    using namespace v8;

    class Deserializer : public ValueDeserializer::Delegate {
    public:
        Deserializer(Isolate* isolate, std::shared_ptr<SerializedData> data);

        MaybeLocal<Value> ReadValue();

    private:
        Isolate* _isolate;
        ValueDeserializer _deserializer;
        std::shared_ptr<SerializedData> _data;

        Deserializer(const Deserializer&) = delete;
        Deserializer& operator=(const Deserializer&) = delete;
    };


    Deserializer::Deserializer(Isolate* isolate, std::shared_ptr<SerializedData> data) :
        _isolate(isolate),
        _deserializer(isolate, data->data(), data->size(), this),
        _data(data) {
        _deserializer.SetSupportsLegacyWireFormat(true);
    }

    MaybeLocal<Value> Deserializer::ReadValue() {
        bool read_header;
        Local<Context> context = _isolate->GetCurrentContext();
        if (!_deserializer.ReadHeader(context).To(&read_header)) {
            return MaybeLocal<Value>();
        }

        uint32_t index = 0;
        Local<String> key = v8_helpers::MakeV8String(_isolate, "_externalized");
        for (const auto& contents : _data->shared_array_buffer_contents()) {
            Local<SharedArrayBuffer> shared_array_buffer = SharedArrayBuffer::New(
                _isolate, contents.first.Data(), contents.first.ByteLength());
            auto sharedPtrWrap = napa::module::binding::CreateShareableWrap(contents.second);
            shared_array_buffer->CreateDataProperty(context, key, sharedPtrWrap);
            _deserializer.TransferSharedArrayBuffer(index++, shared_array_buffer);
        }

        return _deserializer.ReadValue(context);
    }
}
}
