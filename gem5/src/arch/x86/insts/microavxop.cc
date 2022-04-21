#include "arch/x86/insts/microavxop.hh"

#include "arch/x86/regs/misc.hh"
#include "arch/x86/utility.hh"
#include "cpu/exec_context.hh"
#include "debug/CSIRowhammer.hh"
#include "CSIRowhammer/IntegrityInformation.hh"

#include <string>
namespace X86ISA
{
  std::string
  AVXOpBase::generateDisassembly(Addr pc,
                                 const ::Loader::SymbolTable *symtab) const
  {
    std::stringstream response;

    printMnemonic(response, instMnem, mnemonic);
    printDestReg(response, 0, destSize);
    if (this->srcType == SrcType::Non)
    {
      return response.str();
    }
    response << ", ";
    printSrcReg(response, 0, srcSize);
    switch (this->srcType)
    {
    case RegReg:
    {
      response << ", ";
      printSrcReg(response, 1, srcSize);
      break;
    }
    case RegImm:
    {
      ccprintf(response, ", %#x", imm8);
      break;
    }
    case RegRegImm:
    {
      response << ", ";
      printSrcReg(response, 1, srcSize);
      ccprintf(response, ", %#x", imm8);
      break;
    }
    case RegRegReg:
    {
      response << ", ";
      printSrcReg(response, 1, srcSize);
      response << ", ";
      printSrcReg(response, 2, srcSize);
      break;
    }
    default:
      break;
    }
    return response.str();
  }

  AVXOpBase::FloatInt AVXOpBase::calcPackedBinaryOp(FloatInt src1, FloatInt src2,
                                                    BinaryOp op) const
  {
    FloatInt dest;
    if (this->srcSize == 4)
    {
      // 2 float.
      switch (op)
      {
      case BinaryOp::FloatAdd:
        dest.f.f1 = src1.f.f1 + src2.f.f1;
        dest.f.f2 = src1.f.f2 + src2.f.f2;
        break;
      case BinaryOp::FloatSub:
        dest.f.f1 = src1.f.f1 - src2.f.f1;
        dest.f.f2 = src1.f.f2 - src2.f.f2;
        break;
      case BinaryOp::FloatMul:
        dest.f.f1 = src1.f.f1 * src2.f.f1;
        dest.f.f2 = src1.f.f2 * src2.f.f2;
        break;
      case BinaryOp::FloatDiv:
        dest.f.f1 = src1.f.f1 / src2.f.f1;
        dest.f.f2 = src1.f.f2 / src2.f.f2;
        break;
      case BinaryOp::IntAdd:
        dest.si.i1 = src1.si.i1 + src2.si.i1;
        dest.si.i2 = src1.si.i2 + src2.si.i2;
        break;
      case BinaryOp::IntXOR:
        dest.si.i1 = src1.si.i1 ^ src2.si.i1;
        dest.si.i2 = src1.si.i2 ^ src2.si.i2;
        break;
      case BinaryOp::IntSub:
        dest.si.i1 = src1.si.i1 - src2.si.i1;
        dest.si.i2 = src1.si.i2 - src2.si.i2;
        break;
      case BinaryOp::IntAnd:
        dest.si.i1 = src1.si.i1 & src2.si.i1;
        dest.si.i2 = src1.si.i2 & src2.si.i2;
        break;
      case BinaryOp::SIntMin:
        dest.si.i1 = std::min(src1.si.i1, src2.si.i1);
        dest.si.i2 = std::min(src1.si.i2, src2.si.i2);
        break;
      default:
        assert(false && "Invalid op type.");
      }
    }
    else
    {
      // 1 double;
      switch (op)
      {
      case BinaryOp::FloatAdd:
        dest.d = src1.d + src2.d;
        break;
      case BinaryOp::FloatSub:
        dest.d = src1.d - src2.d;
        break;
      case BinaryOp::FloatMul:
        dest.d = src1.d * src2.d;
        break;
      case BinaryOp::FloatDiv:
        dest.d = src1.d / src2.d;
        break;
      case BinaryOp::IntAdd:
        dest.sl = src1.sl + src2.sl;
        break;
      case BinaryOp::IntSub:
        dest.sl = src1.sl - src2.sl;
        break;
      case BinaryOp::IntAnd:
        dest.sl = src1.sl & src2.sl;
        break;
      case BinaryOp::SIntMin:
        dest.sl = std::min(src1.sl, src2.sl);
        break;
      default:
        assert(false && "Invalid op type.");
      }
    }
    return dest;
  }

  void AVXOpBase::doPackedBinaryOp(ExecContext *xc, BinaryOp op) const
  {
    auto vRegs = destVL / sizeof(uint64_t);
    FloatInt src1;
    FloatInt src2;
    for (int i = 0; i < vRegs; i++)
    {
      src1.ul = xc->readFloatRegOperandBits(this, i * 2 + 0);
      src2.ul = xc->readFloatRegOperandBits(this, i * 2 + 1);
      auto dest = this->calcPackedBinaryOp(src1, src2, op);
      xc->setFloatRegOperandBits(this, i, dest.ul);
    }
  }

  void AVXOpBase::doFusedPackedBinaryOp(ExecContext *xc, BinaryOp op1,
                                        BinaryOp op2) const
  {
    auto vRegs = destVL / sizeof(uint64_t);
    FloatInt src1;
    FloatInt src2;
    FloatInt src3;
    for (int i = 0; i < vRegs; i++)
    {
      src1.ul = xc->readFloatRegOperandBits(this, i * 3 + 0);
      src2.ul = xc->readFloatRegOperandBits(this, i * 3 + 1);
      src3.ul = xc->readFloatRegOperandBits(this, i * 3 + 2);
      auto tmp = this->calcPackedBinaryOp(src1, src2, op1);
      auto dest = this->calcPackedBinaryOp(tmp, src3, op2);
      xc->setFloatRegOperandBits(this, i, dest.ul);
    }
  }

  // CSI:Rowhammer: This method computes the MAC
  void AVXOpBase::CSI_MAC(ExecContext *xc) const
  {
    auto vRegs = srcVL / sizeof(uint64_t);
    RegVal *src1 = new RegVal[vRegs];
    
    RegVal physicalAddress = xc->readIntRegOperand(this, 0);
    //DPRINTF(CSIRowhammer, "Physical address is %llx\n", physicalAddress);
    
    // Load values from 64 bit registers to array to emulate 512 bit register
    for (int i = 0; i < vRegs; i++)
    {
      src1[i] = xc->readFloatRegOperandBits(this, i + 1);
      //DPRINTF(CSIRowhammer, "src1[%d] is %llx \n", i, src1[i]);
    }
    
    auto mac = IntegrityInformation::computeMAC((uint8_t *)src1, physicalAddress, 56);
    //DPRINTF(CSIRowhammer, "Calculated MAC is %llx\n", mac);

    xc->setIntRegOperand(this, 0, mac);
  }

  // CSI:Rowhammer
  void AVXOpBase::CSI_compare(ExecContext *xc) const
  {
    auto vRegs = destVL / sizeof(uint64_t);

    bool equal = true;

    // Load values from 64 bit registers to array to emulate 512 bit register
    for (int i = 0; i < vRegs; i++)
    {
      if (xc->readFloatRegOperandBits(this, i * 2 + 0) !=
          xc->readFloatRegOperandBits(this, i * 2 + 1))
      {
        equal = false;
      }
    }

    RFLAGS rflags = (RFLAGS)xc->readMiscReg(MISCREG_RFLAGS);
    rflags.zf = (equal == true);
    
    // We need to do this additionally
    X86ISA::setRFlags(xc->tcBase(), rflags);
    xc->setMiscReg(MISCREG_RFLAGS, rflags);
  }

} // namespace X86ISA
