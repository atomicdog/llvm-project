from textwrap import dedent
import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from lldbsuite.test.gdbclientutils import *
from lldbsuite.test.lldbgdbclient import GDBRemoteTestBase

# Reading an HCS08 variable that lives in the index register.
#
# H:X is the only allocatable 16-bit register on this machine, so it is where a
# pointer or an int actually lives and how a 16-bit argument arrives - the
# compiler emits DW_OP_reg3 for it constantly. It is also the one register a
# stub cannot simply hand over: the hardware has the two 8-bit halves H and X,
# and H:X is the pair of them rather than a seventh register. So the target XML
# has to declare it a composite with value_regnums, and LLDB has to assemble the
# two halves in the right order.
#
# That order is the point of the test. H is the high byte, so h,x on a
# big-endian target must give 0x1234 here; assembling them backwards gives
# 0x3412, which is a wrong answer rather than an error. Nothing else in the
# tree exercises value_regnums with more than one source register - every other
# use is a sub-register slice like w0 of x0 - so this path was untested.
#
# hcs08-hx.yaml and the register values below both come from one build of
# hxprobe.c, halted on a simulator at the first instruction of `scale`, where
# the DWARF says the incoming argument `v` is in DW_OP_reg3.

PC_SCALE = 0x800C
PC_RUN = 0x8020
SP_SCALE = 0x07FB

H_HALF = 0x12
X_HALF = 0x34
HX = 0x1234  # not 0x3412

STACK_ADDR = 0x07E0
STACK = bytes.fromhex(
    "0000000000000000000000000000000000000000000000000000000080208007"
)


class MyResponder(MockGDBServerResponder):
    def qSupported(self, client_supported):
        return "PacketSize=4000;qXfer:features:read+"

    def qXferRead(self, obj, annex, offset, length):
        if annex != "target.xml":
            return None, False
        # What a real HCS08 stub has to serve. The DWARF numbers are the ones
        # the compiler emits (CodeGenDesign.md section 23): a=0 h=1 x=2 hx=3
        # sp=4 pc=5. Only the six hardware registers occupy space in a `g`
        # packet - hx is composed from h and x and takes none.
        #
        # The dwarf_regnum attributes are in fact redundant: these are the
        # canonical register names, so the ABI plugin's table supplies the same
        # numbers through AugmentRegisterInfo, and dropping them here still
        # passes. They are stated because a stub should say what it means
        # rather than lean on matching LLDB's spelling of the names.
        return (
            dedent(
                """\
            <?xml version="1.0"?>
              <target version="1.0">
                <architecture>hcs08</architecture>
                <feature name="org.gnu.gdb.hcs08.core">
                  <reg name="a" regnum="0" bitsize="8" dwarf_regnum="0" type="int"/>
                  <reg name="h" regnum="1" bitsize="8" dwarf_regnum="1" type="int"/>
                  <reg name="x" regnum="2" bitsize="8" dwarf_regnum="2" type="int"/>
                  <reg name="sp" regnum="3" bitsize="16" dwarf_regnum="4" generic="sp" type="data_ptr"/>
                  <reg name="pc" regnum="4" bitsize="16" dwarf_regnum="5" generic="pc" type="code_ptr"/>
                  <reg name="ccr" regnum="5" bitsize="8" generic="flags" type="int"/>
                  <reg name="hx" regnum="6" bitsize="16" dwarf_regnum="3" type="data_ptr"
                       value_regnums="1,2" invalidate_regnums="1,2"/>
                </feature>
              </target>"""
            ),
            False,
        )

    def haltReason(self):
        return "T0500:00;01:12;02:34;03:07fb;04:800c;05:60;"

    def cont(self):
        return self.haltReason()

    def readRegisters(self):
        # a h x sp pc ccr, in regnum order and target byte order: eight bytes.
        return "00" + "12" + "34" + "07fb" + "800c" + "60"

    def readMemory(self, addr, length):
        out = ""
        for i in range(length):
            byte = addr + i - STACK_ADDR
            out += "%02x" % (STACK[byte] if 0 <= byte < len(STACK) else 0)
        return out


class TestHCS08IndexRegister(GDBRemoteTestBase):
    @skipIfLLVMTargetMissing("HCS08")
    def test(self):
        """Assemble H:X from its halves and read a variable out of it."""
        target = self.createTarget("hcs08-hx.yaml")
        self.server.responder = MyResponder()

        if self.TraceOn():
            self.runCmd("log enable gdb-remote packets")
            self.addTearDownHook(lambda: self.runCmd("log disable gdb-remote packets"))

        process = self.connect(target)
        lldbutil.expect_state_changes(
            self, self.dbg.GetListener(), process, [lldb.eStateStopped]
        )
        thread = process.GetThreadAtIndex(0)
        frame = thread.GetFrameAtIndex(0)

        # The stub declared seven registers; six of them are real.
        regs = frame.GetRegisters().GetValueAtIndex(0)
        by_name = {r.GetName(): r.GetValueAsUnsigned() for r in regs}
        self.assertEqual(
            by_name,
            {
                "a": 0x00,
                "h": H_HALF,
                "x": X_HALF,
                "sp": SP_SCALE,
                "pc": PC_SCALE,
                "ccr": 0x60,
                "hx": HX,
            },
        )

        # Said plainly, because it is the whole point: H is the high half. A
        # client that concatenated the halves the other way round would report
        # 0x3412 here and would not otherwise complain.
        self.assertEqual(frame.FindRegister("hx").GetValueAsUnsigned(), HX)
        self.assertNotEqual(
            frame.FindRegister("hx").GetValueAsUnsigned(),
            (X_HALF << 8) | H_HALF,
            "H:X assembled from the wrong end",
        )

        # And the payoff: at this PC the DWARF puts `v` in DW_OP_reg3, so
        # reading the variable goes through the composite. Without the XML
        # declaring hx, this is the read that comes back unavailable.
        v = frame.FindVariable("v")
        self.assertTrue(v.IsValid(), "v should be in scope")
        self.assertEqual(v.GetValueAsSigned(), HX)

        # Unwinding still works with an XML-supplied register set, which is a
        # different path to the built-in fallback that TestHCS08Backtrace uses.
        self.assertGreaterEqual(len(thread.frames), 2)
        self.assertEqual(thread.GetFrameAtIndex(0).GetFunctionName(), "scale")
        self.assertEqual(thread.GetFrameAtIndex(1).GetFunctionName(), "run")
        self.assertEqual(
            thread.GetFrameAtIndex(1).GetPCAddress().GetLoadAddress(target), PC_RUN
        )
