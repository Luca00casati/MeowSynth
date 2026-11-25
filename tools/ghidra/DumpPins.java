// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Args: <outFile> name=0xfunc ...   For each ListInterface function, record each call and the
// string literals / small immediates referenced since the previous call to a pin-adding function.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;
public class DumpPins extends GhidraScript {
  String str(Address a) {
    try {
      Memory m = currentProgram.getMemory(); MemoryBlock b = m.getBlock(a);
      if (b == null || b.isExecute()) return null;
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < 300; i++) { int c = m.getByte(a.add(i)) & 0xff; if (c == 0) break; if (c < 0x20 && c != 10 && c != 13 || c > 0x7e) return null; sb.append((char) c); }
      return sb.toString();
    } catch (Exception e) { return null; }
  }
  public void run() throws Exception {
    String[] a = getScriptArgs();
    PrintWriter w = new PrintWriter(new FileWriter(a[0]));
    for (int i = 1; i < a.length; i++) {
      String[] kv = a[i].split("=");
      Function f = getFunctionAt(toAddr(kv[1]));
      if (f == null) f = createFunction(toAddr(kv[1]), null);
      w.println("## " + kv[0] + " " + kv[1]);
      List<String> pend = new ArrayList<>(); List<String> imms = new ArrayList<>();
      InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
      while (it.hasNext()) {
        Instruction ins = it.next();
        for (int op = 0; op < ins.getNumOperands(); op++)
          for (Object o : ins.getOpObjects(op))
            if (o instanceof Scalar) {
              long v = ((Scalar) o).getUnsignedValue();
              String s = v > 0x10000000L && v < 0x10400000L ? str(toAddr(v)) : null;
              if (s != null) pend.add(s);
              else if (ins.getMnemonicString().equals("PUSH") && v < 0x100) imms.add(Long.toString(v));
            }
        if (ins.getMnemonicString().equals("CALL")) {
          Address[] fl = ins.getFlows();
          String tgt = fl.length > 0 ? fl[0].toString() : "?";
          if (!pend.isEmpty() || imms.size() >= 3) {
            w.println("CALL " + tgt + " imm=" + imms + " str=" + pend);
            if (imms.size() >= 3) { pend.clear(); }
          }
          imms.clear();
        }
      }
    }
    w.close();
  }
}
