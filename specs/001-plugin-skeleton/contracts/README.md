# Contracts: Disrumpo Plugin Skeleton

This directory would normally contain API contracts (OpenAPI, GraphQL schemas, etc.) for the feature.

## Skeleton Scope

The plugin skeleton does not define new API contracts beyond the standard VST3 SDK interfaces:

- **IAudioProcessor** - Audio processing interface
- **IEditController** - Parameter editing interface
- **IComponent** - State serialization interface

These interfaces are defined by the VST3 SDK and implemented by the Processor and Controller classes.

## State Serialization "Contract"

The only custom data format is the state serialization, documented in [data-model.md](../data-model.md):

```
Binary State Format (v1):
Offset  Size   Type    Field
------  ----   ----    -----
0       4      int32   version (= 1)
4       4      float   inputGain
8       4      float   outputGain
12      4      float   globalMix
```

## Future Contracts

When the plugin expands, this directory may contain:
- Parameter enumeration schema
- Preset file format specification
- IPC message formats (if using IMessage)
