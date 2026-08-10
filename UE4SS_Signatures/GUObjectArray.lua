-- UE4SS Signature Override: GUObjectArray
-- 
-- The PalServer Linux binary is stripped. This file tells UE4SS where to find GUObjectArray.
--
-- Option 1: Return the address directly (if you know it from Ghidra)
--   Replace 0x0 with the actual hex address
--
-- Option 2: Use AOB scanning (if you have a byte pattern)
--   Uncomment the Register/OnMatchFound functions and provide the AOB
--
-- The GUObjectArray is a global variable of type FUObjectArray*.
-- On UE5.1, FUObjectArray is 0xB8 bytes. The NumElements field is at offset 0x24.

-- Option 1: Direct address (uncomment and replace 0x0 with actual address)
return 0x0

-- Option 2: AOB scanning (uncomment if you have a byte pattern)
-- function Register()
--     return "YOUR_AOB_PATTERN_HERE"
-- end
--
-- function OnMatchFound(MatchAddress)
--     -- If the AOB directly points to GUObjectArray, return MatchAddress
--     -- If the AOB is in code that references GUObjectArray via RIP-relative addressing:
--     --   local disp_offset = MatchAddress + OFFSET_TO_DISPLACEMENT
--     --   local disp = DerefToInt32(disp_offset)
--     --   local target = disp_offset + 4 + disp
--     --   return target
--     return MatchAddress
-- end
