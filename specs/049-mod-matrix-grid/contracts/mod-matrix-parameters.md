# Contract: Modulation Matrix Parameters

**Spec**: 049-mod-matrix-grid | **Type**: VST Parameter Registration

## Parameter IDs (plugin_ids.h)

56 new parameters in range 1300-1355, organized as 8 global route slots.

### Base Route Parameters (3 per slot, 24 total: IDs 1300-1323)

```cpp
// Slot N: Source = 1300 + N*3, Destination = 1301 + N*3, Amount = 1302 + N*3
kModSlot0SourceId = 1300,      // StringListParameter, 10 items
kModSlot0DestinationId = 1301, // StringListParameter, 11 items
kModSlot0AmountId = 1302,      // RangeParameter, min=-1.0, max=+1.0
kModSlot1SourceId = 1303,
kModSlot1DestinationId = 1304,
kModSlot1AmountId = 1305,
kModSlot2SourceId = 1306,
kModSlot2DestinationId = 1307,
kModSlot2AmountId = 1308,
kModSlot3SourceId = 1309,
kModSlot3DestinationId = 1310,
kModSlot3AmountId = 1311,
kModSlot4SourceId = 1312,
kModSlot4DestinationId = 1313,
kModSlot4AmountId = 1314,
kModSlot5SourceId = 1315,
kModSlot5DestinationId = 1316,
kModSlot5AmountId = 1317,
kModSlot6SourceId = 1318,
kModSlot6DestinationId = 1319,
kModSlot6AmountId = 1320,
kModSlot7SourceId = 1321,
kModSlot7DestinationId = 1322,
kModSlot7AmountId = 1323,
```

### Detail Parameters (4 per slot, 32 total: IDs 1324-1355)

```cpp
// Slot N: Curve = 1324 + N*4, Smooth = 1325 + N*4, Scale = 1326 + N*4, Bypass = 1327 + N*4
kModSlot0CurveId = 1324,       // StringListParameter, 4 items
kModSlot0SmoothId = 1325,      // RangeParameter, min=0.0, max=100.0 (ms)
kModSlot0ScaleId = 1326,       // StringListParameter, 5 items
kModSlot0BypassId = 1327,      // Parameter (boolean, 0.0 or 1.0)
kModSlot1CurveId = 1328,
kModSlot1SmoothId = 1329,
kModSlot1ScaleId = 1330,
kModSlot1BypassId = 1331,
kModSlot2CurveId = 1332,
kModSlot2SmoothId = 1333,
kModSlot2ScaleId = 1334,
kModSlot2BypassId = 1335,
kModSlot3CurveId = 1336,
kModSlot3SmoothId = 1337,
kModSlot3ScaleId = 1338,
kModSlot3BypassId = 1339,
kModSlot4CurveId = 1340,
kModSlot4SmoothId = 1341,
kModSlot4ScaleId = 1342,
kModSlot4BypassId = 1343,
kModSlot5CurveId = 1344,
kModSlot5SmoothId = 1345,
kModSlot5ScaleId = 1346,
kModSlot5BypassId = 1347,
kModSlot6CurveId = 1348,
kModSlot6SmoothId = 1349,
kModSlot6ScaleId = 1350,
kModSlot6BypassId = 1351,
kModSlot7CurveId = 1352,
kModSlot7SmoothId = 1353,
kModSlot7ScaleId = 1354,
kModSlot7BypassId = 1355,
```

## Parameter Registration (Controller::initialize)

### Source Parameters (StringListParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto sourceId = kModSlot0SourceId + slot * 3;
    auto* param = new Steinberg::Vst::StringListParameter(
        USTRING("Mod Source"),  // title
        sourceId,               // tag
        nullptr                 // units
    );
    // 10 global sources
    param->appendString(USTRING("ENV 1"));
    param->appendString(USTRING("ENV 2"));
    param->appendString(USTRING("ENV 3"));
    param->appendString(USTRING("Voice LFO"));
    param->appendString(USTRING("Gate Output"));
    param->appendString(USTRING("Velocity"));
    param->appendString(USTRING("Key Track"));
    param->appendString(USTRING("Macros 1-4"));
    param->appendString(USTRING("Chaos/Rungler"));
    param->appendString(USTRING("LFO 1-2"));
    parameters.addParameter(param);
}
```

### Destination Parameters (StringListParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto destId = kModSlot0DestinationId + slot * 3;
    auto* param = new Steinberg::Vst::StringListParameter(
        USTRING("Mod Dest"),
        destId,
        nullptr
    );
    // 11 global destinations (7 voice + 4 global-scope)
    param->appendString(USTRING("Filter Cutoff"));
    param->appendString(USTRING("Filter Resonance"));
    param->appendString(USTRING("Morph Position"));
    param->appendString(USTRING("Distortion Drive"));
    param->appendString(USTRING("TranceGate Depth"));
    param->appendString(USTRING("OSC A Pitch"));
    param->appendString(USTRING("OSC B Pitch"));
    param->appendString(USTRING("Global Filter Cut"));
    param->appendString(USTRING("Global Filter Res"));
    param->appendString(USTRING("Master Volume"));
    param->appendString(USTRING("Effect Mix"));
    parameters.addParameter(param);
}
```

### Amount Parameters (RangeParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto amountId = kModSlot0AmountId + slot * 3;
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        USTRING("Mod Amount"),
        amountId,
        nullptr,     // units
        -1.0,        // minPlain
        1.0,         // maxPlain
        0.0          // defaultPlain
    ));
}
```

### Curve Parameters (StringListParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto curveId = kModSlot0CurveId + slot * 4;
    auto* param = new Steinberg::Vst::StringListParameter(
        USTRING("Mod Curve"),
        curveId,
        nullptr
    );
    param->appendString(USTRING("Linear"));
    param->appendString(USTRING("Exponential"));
    param->appendString(USTRING("Logarithmic"));
    param->appendString(USTRING("S-Curve"));
    parameters.addParameter(param);
}
```

### Smooth Parameters (RangeParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto smoothId = kModSlot0SmoothId + slot * 4;
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        USTRING("Mod Smooth"),
        smoothId,
        USTRING("ms"),
        0.0,         // minPlain
        100.0,       // maxPlain
        0.0          // defaultPlain
    ));
}
```

### Scale Parameters (StringListParameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto scaleId = kModSlot0ScaleId + slot * 4;
    auto* param = new Steinberg::Vst::StringListParameter(
        USTRING("Mod Scale"),
        scaleId,
        nullptr
    );
    param->appendString(USTRING("x0.25"));
    param->appendString(USTRING("x0.5"));
    param->appendString(USTRING("x1"));
    param->appendString(USTRING("x2"));
    param->appendString(USTRING("x4"));
    param->getInfo().defaultNormalizedValue = 2.0 / 4.0; // x1 is index 2
    parameters.addParameter(param);
}
```

### Bypass Parameters (Parameter)

```cpp
for (int slot = 0; slot < 8; ++slot) {
    auto bypassId = kModSlot0BypassId + slot * 4;
    parameters.addParameter(new Steinberg::Vst::Parameter(
        USTRING("Mod Bypass"),
        bypassId,
        nullptr,     // units
        0.0,         // defaultNormalized (off)
        1,           // stepCount (boolean)
        0            // flags
    ));
}
```

## State Save/Load

Global modulation parameters are saved/loaded automatically via the standard VST3 parameter state mechanism (`getState()`/`setState()`). No custom serialization needed for global routes.

Voice routes are saved separately via IMessage exchange during `setComponentState()`.
