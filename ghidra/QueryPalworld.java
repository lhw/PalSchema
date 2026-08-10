//@category PalSchema
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class QueryPalworld extends GhidraScript {
    private void line(String s) { println(s); }

    private void dumpBytes(Address address, int count) throws Exception {
        byte[] bytes = new byte[count];
        currentProgram.getMemory().getBytes(address, bytes);
        StringBuilder out = new StringBuilder();
        for (byte b : bytes) out.append(String.format("%02x ", b & 0xff));
        line("  bytes " + out);
    }

    private void dumpRefs(Address address) {
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function fn = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
            line("  ref " + ref.getFromAddress() + " in " + (fn == null ? "<no function>" : fn.getEntryPoint()));
        }
    }

    private void dumpVtable(Symbol symbol) throws Exception {
        Address base = symbol.getAddress();
        line("VTABLE " + symbol.getName() + " " + base);
        for (int i = 0; i < 120; i++) {
            Address slot = base.add(i * 8L);
            long value = currentProgram.getMemory().getLong(slot);
            if (value == 0 || value == -1 || value == -40) {
                if (i > 5) break;
                continue;
            }
            Address target = toAddr(value);
            Function fn = currentProgram.getFunctionManager().getFunctionAt(target);
            String detail = "";
            if (fn != null) detail = String.format(" %s..%s", fn.getEntryPoint(), fn.getBody().getMaxAddress());
            line(String.format("  [%d] %s %s%s", i, target, fn == null ? "" : fn.getName(), detail));
            if (i < 8) dumpBytes(target, 24);
        }
    }

    @Override
    public void run() throws Exception {
        line("IMAGE_BASE " + currentProgram.getImageBase());
        SymbolTable table = currentProgram.getSymbolTable();
        SymbolIterator symbols = table.getAllSymbols(true);
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            String name = symbol.getName();
            if (name.equals("_ZTV17UPalItemContainer") || name.equals("_ZTV12UPalItemSlot") ||
                name.equals("_ZTV29UPalDynamicItemWorldSubsystem") || name.equals("_ZTV6UWorld")) {
                dumpVtable(symbol);
            }
        }

        String[] needles = {
            "C:/works/Pal-UE-App/Source/Pal/PalItemContainer.cpp",
            "C:/works/Pal-UE-App/Source/Pal/PalDynamicItemWorldSubsystem.cpp",
            "ApplySaveData", "UpdateItem_ServerInternal", "CleanupWorld", "Create_ServerInternal"
        };
        Memory memory = currentProgram.getMemory();
        for (String needle : needles) {
            byte[] bytes = needle.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            Address found = memory.findBytes(memory.getMinAddress(), memory.getMaxAddress(), bytes, null, true, monitor);
            if (found != null) {
                line("STRING " + needle + " " + found);
                dumpRefs(found);
            } else {
                line("STRING-MISS " + needle);
            }
        }

        long[] interesting = {0x7184800L, 0x7184b20L, 0x7009fa0L, 0x729a7f0L};
        for (long value : interesting) {
            Address address = toAddr(value);
            Function fn = currentProgram.getFunctionManager().getFunctionContaining(address);
            line("ADDRESS " + address + " FUNCTION " + (fn == null ? "<none>" : fn.getEntryPoint() + " " + fn.getName()));
            dumpBytes(address, 32);
        }

        int functions = 0;
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
        while (iterator.hasNext()) { iterator.next(); functions++; }
        line("FUNCTION_COUNT " + functions);
    }
}
