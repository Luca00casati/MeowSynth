// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
public class DecompileAt extends GhidraScript {
  public void run() throws Exception {
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    for (String a : getScriptArgs()) {
      Function f = getFunctionContaining(toAddr(a));
      if (f == null) { f = createFunction(toAddr(a), null); }
      println("// " + f.getName() + " @ " + f.getEntryPoint());
      println(d.decompileFunction(f, 60, monitor).getDecompiledFunction().getC());
    }
  }
}
