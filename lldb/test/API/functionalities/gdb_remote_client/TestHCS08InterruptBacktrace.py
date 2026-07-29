import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from lldbsuite.test.gdbclientutils import *
from lldbsuite.test.lldbgdbclient import GDBRemoteTestBase

# Unwinding *out of* an interrupt handler, from its very first instruction.
#
# An ordinary call pushes two bytes, so the CFA on entry is SP+2. An interrupt
# does not: the hardware stacks five - PCL, PCH, X, A and the condition codes -
# before the handler's first instruction runs, and a compiled handler then adds
# its own pshh for H. So the handler's entry CFA is SP+5, and nothing about a
# function's address says which kind it is. Only its CFI does.
#
# That matters at exactly one place, and it is the place a debugger lands:
# `breakpoint set --name <handler>` puts the breakpoint on the handler's first
# instruction, and LLDB asks the ABI for an architectural "at function entry"
# unwind plan there and prefers it over the function's own CFI. An
# architectural answer has to assume `jsr`, so it says SP+2 and the walk leaves
# the interrupt frame three bytes short - which does not fail, it reports a
# caller that was never called. The HCS08 ABI plugin therefore supplies no
# entry plan at all, so the DWARF is used; see the comment on
# ABIHCS08::CreateFunctionEntryUnwindPlan.
#
# The chain here is five deep on purpose. Leaving the interrupt frame at the
# wrong place is an error that accumulates rather than cancelling, so the
# frames *after* the handler are what show it was left in the right place.
#
# hcs08-isr.yaml and the values below both come from one build of isrbt.c, and
# the registers and stack are what an MC9S08AW60 actually held when it halted
# on the handler's first instruction - read back over BDM, not constructed.

PC_HANDLER = 0x2010  # swi_handler, first instruction (pshh has not run)
PC_DEEP = 0x201B  # the instruction after the `swi`
PC_MIDDLE = 0x2027
PC_OUTER = 0x204E
PC_RUN = 0x2057
PC_START = 0x200B

SP_HANDLER = 0x085A
# CFA = SP + 5: five bytes stacked by the interrupt, and the return address
# straddles it at CFA-1 exactly as it does for a call.
CFA_HANDLER = 0x085F

STACK_ADDR = 0x0840
# Eight bytes per line, as the target reported them. 0x085e holds "20 1b", the
# return address into `deep`, and the frames below follow it up to 0x086e.
STACK = bytes.fromhex(
    "c3b0c0a0898e858d"
    "2979a59dd8d0015c"
    "bcef203e1a0e0015"
    "209000680015201b"
    "2027001500140015"
    "0029204e2057200b"
)


class MyResponder(MockGDBServerResponder):
    def qSupported(self, client_supported):
        return "PacketSize=4000"

    def haltReason(self):
        # a h x sp pc ccr, the built-in HCS08 fallback register set.
        return "T0500:00;01:00;02:15;03:085a;04:2010;05:68;"

    def cont(self):
        return self.haltReason()

    def readMemory(self, addr, length):
        out = ""
        for i in range(length):
            byte = addr + i - STACK_ADDR
            out += "%02x" % (STACK[byte] if 0 <= byte < len(STACK) else 0)
        return out


class TestHCS08InterruptBacktrace(GDBRemoteTestBase):
    @skipIfLLVMTargetMissing("HCS08")
    def test(self):
        """Walk out of an interrupt handler halted on its first instruction."""
        target = self.createTarget("hcs08-isr.yaml")
        self.server.responder = MyResponder()

        if self.TraceOn():
            self.runCmd("log enable lldb unwind")
            self.addTearDownHook(lambda: self.runCmd("log disable lldb unwind"))

        process = self.connect(target)
        lldbutil.expect_state_changes(
            self, self.dbg.GetListener(), process, [lldb.eStateStopped]
        )
        thread = process.GetThreadAtIndex(0)

        # Halted on the handler's first instruction, which is the case the
        # architectural entry plan would get wrong.
        frame0 = thread.GetFrameAtIndex(0)
        self.assertEqual(frame0.GetPCAddress().GetLoadAddress(target), PC_HANDLER)
        self.assertEqual(frame0.GetFunctionName(), "swi_handler")
        self.assertEqual(
            frame0.FindRegister("sp").GetValueAsUnsigned(), SP_HANDLER
        )

        # Five bytes, not two. Getting this wrong is the whole failure mode:
        # the CFA lands at 0x085c, the return address is read from 0x085b, and
        # the caller comes out as 0x6800 - which is in range, so it reads as a
        # frame rather than as an error.
        self.assertEqual(frame0.GetCFA(), CFA_HANDLER)

        # And out through the interrupted function to the bottom of the stack.
        expected = [
            ("swi_handler", PC_HANDLER),
            ("deep", PC_DEEP),
            ("middle", PC_MIDDLE),
            ("outer", PC_OUTER),
            ("run", PC_RUN),
            ("_start", PC_START),
        ]
        self.assertEqual(len(thread.frames), len(expected))
        for idx, (name, pc) in enumerate(expected):
            f = thread.GetFrameAtIndex(idx)
            self.assertEqual(f.GetFunctionName(), name, "frame %d function" % idx)
            self.assertEqual(
                f.GetPCAddress().GetLoadAddress(target), pc, "frame %d pc" % idx
            )

        # The interrupted function's argument survives the crossing, which it
        # only can if the frame below the handler was located correctly.
        self.assertEqual(
            thread.GetFrameAtIndex(2).FindVariable("n").GetValueAsSigned(), 20
        )
