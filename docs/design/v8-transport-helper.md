# v8-transport-helper

## Incentives
The abstraction of 'Transportable' lies in the center of napa.js to efficiently share objects between JavaScript VMs (napa workers). Except JavaScript primitive types, an object needs to implement 'Transportable' interface to make it transportable. It means [Javascript standard built-in objects](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects) are not transportable unless wrappers or equivalent implementations for them are implemented by extending 'Transportable' interface. Developing cost for all those objects is not trivial, and new abstraction layer (wrappers or equivalent implementations) will bring barriers for users to learn and adopt these new stuffs. Moreover, developers also need to deal with the interaction between JavaScript standards objects and those wrappers or equivalent implementations.

The incentive of transport-fallback is to provide a solution to make JavaScript standard built-in objects transportable with requirements listed in the below Goals section.

At the first stage, we will focus on an efficient solution to share data between napa workers. Basically, it is about making SharedArrayBuffer / TypedArray / DataView transportable.

## Goals
Make Javascript built-in objects transportable with
- a efficient way to share structured data, like SharedArrayBuffer, among napa workers
- consistent APIs with ECMA standards
- no new abstraction layers for the simplest usage
- the least new concepts for advanced usage
- a scalable solution to make all Javascript built-in objects transportable, avoiding to make them transportable one by one.

## Example
The below example shows how SharedArrayBuffer object is transported across multiple napa workers. It will print the TypedArray 'ar' created from a SharedArrayBuffer, having its all elements set to 100 by different napa workers. 
```js
var napa = require("napajs");
var zone = napa.zone.create('zone', { workers: 4 });

function foo(sab, i) {
    var ta = new Uint8Array(sab);
    ta[i] = 100;
    return i;
}

function run() {
    var promises = [];
    var sab = new SharedArrayBuffer(4);
    for (var i = 0; i < 4; i++) {
        promises[i] = zone.execute(foo, [sab, i]);
    }

    return Promise.all(promises).then(values => {
        var ta = new Uint8Array(sab);
        console.log(ta);
    });
}

run();

```

## Solution
Here we just give a high level description of the solution. Its api will go to docs/api/transport, and some key concepts or design details will be filled in this file later.
- V8 provides its value serialization solution by ValueSerializer and ValueDeserializer, which is compatible with the HTML structured clone algorithm. It is a horizontal solution to serialize / deserialize JavaScript objects. ValueSerializer::Delegate and ValueDeserializer::Delegate are their inner class. They work as base classes from which developers can deprive to customize some special handling of external / shared resources, like memory used by a SharedArrayBuffer object.

- v8_extensions::ExternalizedContents
> 1. It holds externalized contents (memory) of a SharedArrayBuffer instance once it is serialized in V8TransportHelper::SerializeValue().
> 2. Only 1 instance of ExternalizedContents can be generated for each SharedArrayBuffer. If a SharedArrayBuffer had been externalized, it will reuse the ExternalizedContents instance created before.

- v8_extensions::SerializedData
> 1. It is generated by V8TransportHelper::SerializeValue(). It holds serialized data of an object, which is required during its deserialization.

- V8TransportHelper
> 1. Serializer depriving from ValueSerializer::Delegate
> 2. Deserializer depriving from ValueDeserializer::Delegate
> 3. static std::shared_ptr<SerializedData> V8TransportHelper::SerializeValue(Isolate* isolate, Local<Value> value);
>>> Generate a SerializedData instance by serializing the input value.
>>> If any SharedArrayBuffer instances exist in the input value, their ExternalizedContents instances will be generated and attached to the ShareArrayBuffer instances respectively.
> 4. static MaybeLocal<Value> V8TransportHelper::DeserializeValue(Isolate* isolate, std::shared_ptr<SerializedData> data);
>>> Restore a js value from its SerializedData instance generated by V8TransportHelper::SerializeValue() before.

- Currently, napa relies on Transportable API and a registered constructor to make an object transportable. In [marshallTransform](https://github.com/Microsoft/napajs/blob/master/lib/transport/transport.ts), when a JavaScript object is detected to have a registered constructor, it will go with napa way to marshall this object with the help of a TransportContext object, otherwise a non-transportable error is thrown.

- Instead of throwing an Error when no registered constructor is detected, the above mentioned V8TransportHelper can jump in to help marshall this object. We can also use a whitelist of verified object types to restrict this v8-transport-helper solution.
```js
export function marshallTransform(jsValue: any, context: transportable.TransportContext): any {
     if (jsValue != null && typeof jsValue === 'object' && !Array.isArray(jsValue)) {
        let constructorName = Object.getPrototypeOf(jsValue).constructor.name;
        if (constructorName !== 'Object') {
            if (typeof jsValue['cid'] === 'function') {
                return <transportable.Transportable>(jsValue).marshall(context);
            } else if (_v8TransportHelperVerifiedWhitelist.has(constructorName)) {
                return {v8SerializedData : v8TransportHelper.serializeValue(jsValue)};
            }
            else {
                throw new Error(`Object type \"${constructorName}\" is not transportable.`);
            }
        }
    }
    return jsValue;
}
```
- The reverse process will be invoked in [unmarshallTransform](https://github.com/Microsoft/napajs/edit/master/lib/transport/transport.ts) if the payload is detected to have v8SerializedData property.
```js
function unmarshallTransform(payload: any, context: transportable.TransportContext): any {
    if (payload != null && payload._cid !== undefined) {
        let cid = payload._cid;
        if (cid === 'function') {
            return functionTransporter.load(payload.hash);
        }
        let subClass = _registry.get(cid);
        if (subClass == null) {
            throw new Error(`Unrecognized Constructor ID (cid) "${cid}". Please ensure @cid is applied on the class or transport.register is called on the class.`);
        }
        let object = new subClass();
        object.unmarshall(payload, context);
        return object;
    } else if (payload.hasOwnProperty('v8SerializedData')) {
        return v8TransportHelper.deserializeValue(payload['v8SerializedData']);
    }
    return payload;
}
```


## Constraints
The above solution is based on the serialization / deserialization mechanism of V8. It may have the following constraints.
- Not all [JavaScripts standard built-in objects](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects) are supported by Node (as a dependency of Napa in node mode) or V8 of a given version. We only provide transporting solution for those mature object types.
- Up to present, Node does not explicitly support multiple V8 isolates. There might be inconsistency to transport objects between node zone and napa zones. Extra effort might be required to make it consistent.