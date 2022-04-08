#include <assert.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <QBDI.h>

struct info
{
    QBDI::rword rbp = 0;
    bool start = false;
    QBDI::rword p = 0;
    size_t size = 0;
    std::fstream file;

    info (std::string filename): file {filename, file.out} {}
};

using T = int;

int sink (int* p)
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
	const size_t size = 10;

    T* p = source (size);

    // std::cout << p << std::endl;
    sink(p);

    T* p1 = p + 2;
    sink(p1);

    p1++;
    sink(p1 + 3);

    delete[] p;		

    return 15;
}

QBDI::VMAction callSource (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
	info* p = static_cast <info*> (data);

    p->size = gprState->rdi * sizeof (T);
    p->rbp = gprState->rbp;
    p->start = true;

	return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction retInstrument (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
    info* p = static_cast <info*> (data);
    // std::cout << "2: " << gprState->rbp << std::endl;
    if (p->start and (p->rbp == gprState->rbp))
    {
        p->p = gprState->rax;
        p->start = false;
    }

    return QBDI::VMAction::CONTINUE;
}

QBDI::VMAction sinkInstrument (QBDI::VM *vm, QBDI::GPRState *gprState,
					  			QBDI::FPRState *fprState, void* data)
{
    info* p = static_cast <info*> (data);

    if (gprState->rdi >= p->p and gprState->rdi < p->p + p->size)
    {
        p->file << "access to 0x" << p->p  << " buffer, size: " << p->size << std::endl;
    }

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
    vm.addMnemonicCB ("RET*",  QBDI::POSTINST, retInstrument, &i);
    vm.addCodeAddrCB (reinterpret_cast <QBDI::rword> (source), QBDI::PREINST, callSource, &i);
    vm.addCodeAddrCB (reinterpret_cast <QBDI::rword> (sink), QBDI::PREINST, sinkInstrument, &i);

	res = vm.addInstrumentedModuleFromAddr(reinterpret_cast<QBDI::rword>(TEST1));
	assert(res == true);

	QBDI::rword retvalue;
	res = vm.call(&retvalue, reinterpret_cast<QBDI::rword>(TEST1));
	assert(res == true);

	QBDI::alignedFree(fakestack);
	return 0;
}