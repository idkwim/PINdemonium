#include "HookFunctions.h"


HookFunctions::HookFunctions(void)
{
	this->functionsMap.insert( std::pair<string,int>("VirtualAlloc",VIRTUALALLOC_INDEX) );
	this->functionsMap.insert( std::pair<string,int>("RtlAllocateHeap",RTLALLOCATEHEAP_INDEX) );
	this->functionsMap.insert( std::pair<string,int>("IsDebuggerPresent",ISDEBUGGERPRESENT_INDEX) );
	this->functionsMap.insert( std::pair<string,int>("GetTickCount",3) );

	this->enumSyscalls();
	//this->printSyscalls();
}


HookFunctions::~HookFunctions(void)
{
}


//----------------------------- HOOKED FUNCTIONS -----------------------------//

//hook the VirtualAlloc() in order to retrieve the memory range allocated and build ours data structures
VOID VirtualAllocHook(UINT32 virtual_alloc_size , UINT32 ret_heap_address ){
  
  ProcInfo *proc_info = ProcInfo::getInstance();

  HeapZone hz;
  hz.begin = ret_heap_address;
  hz.size = virtual_alloc_size;
  hz.end = ret_heap_address + virtual_alloc_size;

  //saving this heap zone in the map inside ProcInfo
  proc_info->insertHeapZone(hz); 

}

//hook the  HeapAllocHook() in order to retrieve the memory range allocated and build ours data structures
VOID HeapAllocHook(UINT32 heap_alloc_size , UINT32 ret_heap_address ){
	
	if (heap_alloc_size == 0){
		return;
	}

	ProcInfo *proc_info = ProcInfo::getInstance();

	HeapZone hz;
	hz.begin = ret_heap_address;
	hz.size = heap_alloc_size;
	hz.end = ret_heap_address + heap_alloc_size;

	//saving this heap zone in the map inside ProcInfo
	proc_info->insertHeapZone(hz); 

}

//REMEMBER!!! : PIN wants a function pointer in the AFUNCPTR agument!!!
//avoid the detection of the debugger replacing the function IsDebuggerPresent() with a new one that returns always false
//very basic way to avoid this anti-debugging technique
bool * IsDebuggerPresentHook(){
	return false;
}

double GetTickCountHook(){
	

	
	double cc_now = __rdtsc();

	//printf("CC_NOW: %f\n" , cc_now);

	TimeTracker *ttracker = TimeTracker::getInstance();
	
	double dbi_delay = ttracker->GetDelay();

	MYINFO("dbi_delay is %f\n" , dbi_delay);

	//printf("CC_DELAY: %f\n" , dbi_delay);

	double cc_dbi_start = ttracker->GetStartDbiCC();

	//printf("CC_START: %f\n" ,  cc_dbi_start);

	dbi_delay = (cc_now - cc_dbi_start  - dbi_delay) /1000.0;

	double corrected_cc = cc_dbi_start + dbi_delay;

	MYINFO("corrected cc is %f\n" , corrected_cc);

	corrected_cc = corrected_cc * 0.00000037037f; // let's obtain the ms ( actually the ticks ) 

	MYINFO("Returning from GetTickCount %f\n" , corrected_cc);


	return corrected_cc;

}


//----------------------------- HOOKED DISPATCHER -----------------------------//

//scan the image and try to hook all the function specified above
void HookFunctions::hookDispatcher(IMG img){
	//for each function that we want to hook or replace
	for (std::map<string,int>::iterator item = this->functionsMap.begin(); item != this->functionsMap.end(); ++item){
		//get the pointer to the specified function
		const char * func_name = item->first.c_str();
		RTN rtn = RTN_FindByName( img, func_name);
		//if we found a valid routine
		if(rtn != RTN_Invalid()){
			
			ADDRINT va_address = RTN_Address(rtn);
			MYINFO("Address of %s: %08x\n" ,func_name, va_address);

			RTN_Open(rtn); 	
			int index = item->second;
			MYINFO("index of %s is %d",func_name,index);
			//decide what to do based on the function hooked
			//Different arguments are passed to the hooking routine based on the function
			switch(index){
				case(VIRTUALALLOC_INDEX):
					RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)VirtualAllocHook , IARG_G_ARG1_CALLEE , IARG_G_RESULT0, IARG_END);
					break;
				case(RTLALLOCATEHEAP_INDEX):
					RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)HeapAllocHook , IARG_G_ARG2_CALLEE, IARG_G_RESULT0, IARG_END);
					break;
				case(ISDEBUGGERPRESENT_INDEX):
					RTN_Replace(rtn, AFUNPTR(IsDebuggerPresentHook));
					break;
				case 3:
					   {
						RTN_Replace(rtn, AFUNPTR(GetTickCountHook));
				       }
					break;

			}			
			RTN_Close(rtn);
		}
	}
}


//----------------------------- HOOKING OF SYSCALLS -----------------------------//

// stole this lovely source code from godware from the rreat library.
void HookFunctions::enumSyscalls()
{
    // no boundary checking at all, I assume ntdll is not malicious..
    // besides that, we are in our own process, _should_ be fine..
    unsigned char *image = (unsigned char *) W::GetModuleHandle("ntdll");

    W::IMAGE_DOS_HEADER *dos_header = (W::IMAGE_DOS_HEADER *) image;

    W::IMAGE_NT_HEADERS *nt_headers = (W::IMAGE_NT_HEADERS *)(image +
        dos_header->e_lfanew);

    W::IMAGE_DATA_DIRECTORY *data_directory = &nt_headers->
        OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

    W::IMAGE_EXPORT_DIRECTORY *export_directory =
        (W::IMAGE_EXPORT_DIRECTORY *)(image + data_directory->VirtualAddress);

    unsigned long *address_of_names = (unsigned long *)(image +
        export_directory->AddressOfNames);

    unsigned long *address_of_functions = (unsigned long *)(image +
        export_directory->AddressOfFunctions);

    unsigned short *address_of_name_ordinals = (unsigned short *)(image +
        export_directory->AddressOfNameOrdinals);

    unsigned long number_of_names = MIN(export_directory->NumberOfFunctions,
        export_directory->NumberOfNames);

    for (unsigned long i = 0; i < number_of_names; i++) {

        const char *name = (const char *)(image + address_of_names[i]);

        unsigned char *addr = image + address_of_functions[address_of_name_ordinals[i]];

        if(!memcmp(name, "Zw", 2) || !memcmp(name, "Nt", 2)) {
            // does the signature match?
            // either:   mov eax, syscall_number ; mov ecx, some_value
            // or:       mov eax, syscall_number ; xor ecx, ecx
            // or:       mov eax, syscall_number ; mov edx, 0x7ffe0300
            if(*addr == 0xb8 && (addr[5] == 0xb9 || addr[5] == 0x33 || addr[5] == 0xba)) {
                unsigned long syscall_number = *(unsigned long *)(addr + 1);
				string syscall_name = string(name);
				this->syscallsMap.insert(std::pair<unsigned long,string>(syscall_number,syscall_name));
				
            }
        }
    }
}


void HookFunctions::printSyscalls(){

for(map<unsigned long, string >::const_iterator it = this->syscallsMap.begin(); it !=this->syscallsMap.end(); ++it)
{
    printf("SYSCALL NUMBER %d : %s\n" , it->first , it->second.c_str());

}

}