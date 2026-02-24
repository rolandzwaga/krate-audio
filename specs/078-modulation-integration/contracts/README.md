# Contracts: Arpeggiator Modulation Integration

This feature has no new API contracts. It extends existing internal data structures:

1. **RuinaeModDest enum** -- 5 new values appended (74-78)
2. **kGlobalDestNames array** -- 5 new entries appended (indices 10-14)
3. **kGlobalDestParamIds array** -- 5 new entries appended (indices 10-14)
4. **Processor::applyParamsToEngine()** -- Modified to read mod offsets and apply formulas

All existing contracts (enum values, array indices, parameter IDs) remain unchanged.
