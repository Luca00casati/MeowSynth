// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Args: <outDir> Name=0xaddr ...   Renames each function and writes <outDir>/<Name>.c
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
public class NameAndDecompile extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs(); File out = new File(a[0]); out.mkdirs();
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    for (int i = 1; i < a.length; i++) {
      String[] kv = a[i].split("=");
      Function f = getFunctionAt(toAddr(kv[1]));
      if (f == null) f = createFunction(toAddr(kv[1]), null);
      if (f == null) { println("no function at " + kv[1]); continue; }
      if (!kv[0].equals("-")) try { f.setName(kv[0], SourceType.USER_DEFINED); } catch (Exception e) {}
      DecompileResults r = d.decompileFunction(f, 120, monitor);
      try (PrintWriter w = new PrintWriter(new File(out, (kv[0].equals("-") ? f.getName() : kv[0]) + ".c"))) {
        w.println("// " + f.getName() + " @ " + f.getEntryPoint());
        w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
      }
    }
  }
}
