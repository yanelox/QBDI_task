#include <assert.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <QBDI.h>

#include <set>

struct shadow_mem
{
    std::set <QBDI::rword> regs;
    std::set <QBDI::rword> addrs;
    std::set <QBDI::rword> source_addrs;
};

struct info
{
    QBDI::rword rbp = 0;
    bool start = false;
    QBDI::rword p = 0;
    size_t size = 0;
    size_t count = 0;
    std::fstream file;

    shadow_mem sm;

    info (std::string filename): file {filename, file.out} {}
};

const size_t size = 10;

using T = int;

T globalBuf[size];

int sink (int p)
{
    return 42;
}

T* source(size_t size) 
{
	T* p = new T [size];
    
    // std::cout << p << std::endl;

    return p;
}

int TEST1()
{
    T tmp;
    T localBuf[size];

    T* buf = source (size);

    T start = static_cast <T> (0);

    for (size_t i = 0; i < size; ++i) //to follow which elements leak, all elements have value equal to index
        buf[i] = start++;

    // case 1
    sink(buf[0]); // Leak
    sink(localBuf[0]); // Ok

    // // case 2
    localBuf[1] = buf[1];
    sink(localBuf[1]); // Leak

    // // case 3
    localBuf[2] = buf[2];
    localBuf[3] = localBuf[2];
    localBuf[2] = 'a';
    sink(localBuf[3]); // Leak
    sink(localBuf[2]); // Ok

    // // case 4
    globalBuf[0] = buf[3];
    sink(globalBuf[0]); // Leak

    // // case 5
    globalBuf[1] = buf[4];
    localBuf[0] = globalBuf[1];
    tmp = localBuf[0];
    sink(globalBuf[2]); // Ok
    sink(tmp & 0xff); // Leak
    sink(tmp >> 1 & 0xff); // leak, but print 2, because 4 >> 1 = 2 (leaks value buf[4])

    delete[] buf;		

    return 15;
}

QBDI::VMAction sourceInst (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
	info* p = static_cast <info*> (data);

    p->size = gprState->rdi * sizeof (T);
    p->count = gprState->rdi;
    p->rbp = gprState->rbp;
    p->start = true;

	return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction retInst (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
    info* p = static_cast <info*> (data);
    // std::cout << "2: " << gprState->rbp << std::endl;
    if (p->start and (p->rbp == gprState->rbp))
    {
        p->p = gprState->rax;
        p->start = false;

        for (size_t i = 0; i < p->size; ++i)
            p->sm.source_addrs.insert (p->p + i);

        p->file << "(source) allocated " << p->size << " bytes, address: 0x" << std::hex << p->p << std::endl;
    }

    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction sinkInst (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
    info* p = static_cast <info*> (data);

    // for (auto i:p->sm.regs)
    //     std::cout << i << std::endl;

    if (p->sm.regs.find (5) != p->sm.regs.end()) // 5 - rdi index
        p->file << "(sink) data: " << gprState->rdi << std::endl;

    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction showInstruction(QBDI::VM *vm, QBDI::GPRState *gprState,
                               QBDI::FPRState *fprState, void *data) {
    // Obtain an analysis of the instruction from the VM
    const QBDI::InstAnalysis *instAnalysis = vm->getInstAnalysis();

    // Printing disassembly
    std::cout << std::setbase(16) << instAnalysis->address << ": "
            << instAnalysis->disassembly << std::endl
            << std::setbase(10);

    return QBDI::VMAction::CONTINUE;
}


QBDI::VMAction moveInst (QBDI::VM *vm, QBDI::GPRState *gprState,
                               QBDI::FPRState *fprState, void *data) 
{
    info* p = static_cast <info*> (data);

    const QBDI::InstAnalysis* inst_inf = vm->getInstAnalysis ( QBDI::ANALYSIS_INSTRUCTION 
                                                             | QBDI::ANALYSIS_OPERANDS 
                                                             | QBDI::ANALYSIS_DISASSEMBLY 
                                                             | QBDI::ANALYSIS_SYMBOL);

    size_t size = inst_inf->numOperands;

    QBDI::OperandAnalysis* op_res = inst_inf->operands;

    // std::cout << "------\n";
    // std::cout << inst_inf->disassembly << std::endl;

    // for (int i = 0; i < size; ++i)
    //     std::cout << op_res[i].regCtxIdx << std::endl;

    // std::cout << "------\n";

    if (size == 2 and op_res[0].type == QBDI::OPERAND_GPR and op_res[1].type == QBDI::OPERAND_GPR)
    {
        QBDI::rword reg1 = op_res[0].regCtxIdx;
        QBDI::rword reg2 = op_res[1].regCtxIdx;

        if (reg1 != reg2)
        {
            if (p->sm.regs.find (reg1) != p->sm.regs.end())
            p->sm.regs.erase (reg1);

            if (p->sm.regs.find (reg2) != p->sm.regs.end())
                p->sm.regs.insert (reg1);
        }
    }

    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction andInst (QBDI::VM *vm, QBDI::GPRState *gprState,
                               QBDI::FPRState *fprState, void *data) 
{
    info* p = static_cast <info*> (data);

    const QBDI::InstAnalysis* inst_inf = vm->getInstAnalysis ( QBDI::ANALYSIS_INSTRUCTION 
                                                             | QBDI::ANALYSIS_OPERANDS 
                                                             | QBDI::ANALYSIS_DISASSEMBLY 
                                                             | QBDI::ANALYSIS_SYMBOL);

    size_t size = inst_inf->numOperands;

    QBDI::OperandAnalysis* op_res = inst_inf->operands;

    // std::cout << "------\n";
    // std::cout << inst_inf->disassembly << std::endl;

    // for (int i = 0; i < size; ++i)
    //     std::cout << op_res[i].type << std::endl;

    // std::cout << "------\n";

    // if (size == 2 and op_res[0].type == QBDI::OPERAND_GPR and op_res[1].type == QBDI::OPERAND_GPR)
    // {
    //     QBDI::rword reg1 = op_res[0].regCtxIdx;
    //     QBDI::rword reg2 = op_res[1].regCtxIdx;

    //     if (p->sm.regs.find (reg1) != p->sm.regs.end())
    //         p->sm.regs.erase (reg1);

    //     if (p->sm.regs.find (reg2) != p->sm.regs.end())
    //         p->sm.regs.insert (reg1);
    // }

    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction memInst (QBDI::VM *vm, QBDI::GPRState *gprState,
                               QBDI::FPRState *fprState, void *data)
{
    info* p = static_cast <info*> (data);

    const QBDI::InstAnalysis* inst_inf = vm->getInstAnalysis ( QBDI::ANALYSIS_INSTRUCTION 
                                                             | QBDI::ANALYSIS_OPERANDS 
                                                             | QBDI::ANALYSIS_DISASSEMBLY 
                                                             | QBDI::ANALYSIS_SYMBOL);

    QBDI::OperandAnalysis* op_res = inst_inf->operands;

    std::vector <QBDI::MemoryAccess> mem_acc = vm->getInstMemoryAccess ();

    if (inst_inf->numOperands != 6)
        return QBDI::VMAction::CONTINUE;

    int index = (mem_acc[0].type == QBDI::MEMORY_READ) ? 0 : 1;

    QBDI::rword addr = mem_acc[0].accessAddress;

    if (index == 0)
    {
        QBDI::rword reg = op_res[0].regCtxIdx;

        if (p->sm.regs.find (reg) != p->sm.regs.end())
            p->sm.regs.erase (reg);

        if ((p->sm.addrs.find (addr) != p->sm.addrs.end()) or (p->sm.source_addrs.find (addr) != p->sm.source_addrs.end()))
            p->sm.regs.insert (reg);
    }
    
    else
    {
        QBDI::rword reg  = op_res[5].regCtxIdx;
        
        if (p->sm.addrs.find (addr) != p->sm.addrs.end())
            p->sm.addrs.erase (addr);

        if (p->sm.regs.find (reg) != p->sm.regs.end())
            p->sm.addrs.insert (addr);
    }
    

    return QBDI::VMAction::CONTINUE;
}

static const size_t STACK_SIZE = 0x100000; // 1MB

int main(int argc, char **argv) 
{
    info i{"../log.log"};

	QBDI::VM vm{};  
	QBDI::GPRState* state = vm.getGPRState();
	assert(state != nullptr);

	uint8_t *fakestack;
	bool res = QBDI::allocateVirtualStack(state, STACK_SIZE, &fakestack);
	assert(res == true);

    // vm.addCodeCB(QBDI::PREINST, showInstruction, nullptr);
    vm.addMnemonicCB ("RET*", QBDI::POSTINST, retInst , &i);
    vm.addMnemonicCB ("MOV*", QBDI::POSTINST, moveInst, &i);
    vm.addMnemonicCB ("AND*", QBDI::POSTINST, andInst , &i);

    vm.addMemAccessCB (QBDI::MEMORY_READ_WRITE, memInst, &i);

    vm.addCodeAddrCB (reinterpret_cast <QBDI::rword> (source), QBDI::PREINST, sourceInst, &i);
    vm.addCodeAddrCB (reinterpret_cast <QBDI::rword> (sink), QBDI::PREINST, sinkInst, &i);

	res = vm.addInstrumentedModuleFromAddr(reinterpret_cast<QBDI::rword>(TEST1));
	assert(res == true);

	QBDI::rword retvalue;
	res = vm.call(&retvalue, reinterpret_cast<QBDI::rword>(TEST1));
	assert(res == true);

	QBDI::alignedFree(fakestack);
	return 0;
}