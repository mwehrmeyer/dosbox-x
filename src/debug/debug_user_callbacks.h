/*
 *  debug_user_callbacks.h
 *
 *  This is the place to add your own compiled-in breakpoint callbacks.
 *
 *  A callback is a plain C++ function that the debugger calls every time the CPU reaches
 *  an instruction you bound it to with the "RUNC" command, e.g.:
 *
 *      RUNC 1000:0123 log_regs
 *
 *  After the callback returns, emulation continues (it is a logpoint/tracepoint, not a
 *  stop). Because the callback is compiled into DOSBox-X, it has full, direct access to
 *  the emulator: CPU registers (reg_eax, reg_ax, SegValue(cs), ...), guest memory
 *  (mem_readb / mem_writeb / GetAddress(seg,off)), logging (DEBUG_ShowMsg / LOG_MSG) and
 *  even the debugger command interpreter (ParseCommand).
 *
 *  To add a callback:
 *    1. Write a function with the signature  void my_cb(uint16_t seg, uint32_t off)
 *    2. Register it inside DEBUG_RegisterUserCallbacks() below.
 *    3. Rebuild DOSBox-X. The name you register is what you pass to RUNC
 *       (matched case-insensitively).
 *
 *  This file is #included once into debug.cpp, so everything debug.cpp can see is
 *  available here.
 */

#ifndef DOSBOX_DEBUG_USER_CALLBACKS_H
#define DOSBOX_DEBUG_USER_CALLBACKS_H

// --- Example callbacks ------------------------------------------------------------------

// A minimal callback: count how often the instruction was reached and print AX each time.
static void cb_hello(uint16_t seg, uint32_t off)
{
	static unsigned long hits = 0;
	hits++;
	DEBUG_ShowMsg("RUNC hello: hit #%lu at %04X:%04X, AX=%04X\n", hits, seg, off, reg_ax);
	printf("hello you freak bitches\n");
}


// Log the main general-purpose registers each time the instruction is executed.
static void cb_log_regs(uint16_t seg, uint32_t off)
{
	DEBUG_ShowMsg("RUNC %04X:%04X  AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X\n",
		seg, off, reg_ax, reg_bx, reg_cx, reg_dx, reg_si, reg_di);
}

// Log AX plus the first byte that DS:SI points at - shows reading guest memory.
static void cb_log_ax_dssi(uint16_t /*seg*/, uint32_t /*off*/)
{
	uint8_t value = 0;
	mem_readb_checked((PhysPt)GetAddress(SegValue(ds), reg_si), &value);
	DEBUG_ShowMsg("RUNC: AX=%04X  [DS:SI]=%02X\n", reg_ax, value);
}

// Drive existing debugger commands from native code - shows the ParseCommand passthrough.
static void cb_dump_stack(uint16_t /*seg*/, uint32_t /*off*/)
{
	char cmd[] = "EV SP"; // ParseCommand needs a writable buffer; it copies internally
	ParseCommand(cmd);
}

// --- Registration -----------------------------------------------------------------------
// Add a DEBUG_RegisterCallback line for every callback you want to expose to RUNC.
static void DEBUG_RegisterUserCallbacks(void)
{
	DEBUG_RegisterCallback("hello",       cb_hello);
	DEBUG_RegisterCallback("log_regs",    cb_log_regs);
	DEBUG_RegisterCallback("log_ax_dssi", cb_log_ax_dssi);
	DEBUG_RegisterCallback("dump_stack",  cb_dump_stack);

	// >>> Register your own callbacks here <<<
}

#endif // DOSBOX_DEBUG_USER_CALLBACKS_H
