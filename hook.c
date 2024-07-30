#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dlfcn.h>


uint32_t ret_addr;
uint32_t arg0;
uint32_t arg1;
/*
		从fopen地址跳到裸函数->变成ARM模式
		裸函数可能是ARM模式也可能是Thumb模式->处理4类情况(理论上)ARM-ARM ARM-THUMB THUMB-ARM THUMB-THUMB
		实际考虑两类ARM-ARM THUMB-ARM ARM模式下
*/
void __attribute__((naked)) nakeFun() {
	//裸函数跳回去返回地址，用堆栈保存寄存器，寄存器状态保持不变，返回地址写到PC
	//将需要的push进栈（汇编代码需要的）
	asm("STMFD sp!, {R0,R1,R4-R6,LR}");// PUSH      {R0,R1,R4-R6,LR}
	//自己需要的push压栈
	asm("STMFD sp!, {R0-R12,LR,PC}");
	//保存通用寄存器CPSR
	asm("mrs r0, cpsr");
	asm("STMFD sp!, {R0}");
	////====================================================================
	//获取fopen传入的两个参数
	asm("ldr r4, [sp, #4]");
	asm("str r4, %0":"=m"(arg0));
	asm("ldr r4, [sp, #8]");
	asm("str r4, %0":"=m"(arg1));
	////--------------------------------------------------------------------
	asm("ldr r0, [sp, #4]");                    //pop  R0->R0
	asm("str r0, [sp, #0x1c]");                 //MOV  R6, R0
	asm("ldr r0, [sp, #8]");                    //pop  R1->R0
	asm("str r0, [sp, #4]");                    //MOV  R0, R1
	asm("mov r0, sp");                          //add  R1, SP, #4
	asm("add r0, r0, #0x44");
	asm("str r0, [sp, #8]");
	//ret_addr返回地址给R0
	asm("ldr r0, %0"::"m"(ret_addr));           //行列汇编语法格式==伪指令
	//sp与pc有3C的偏移，R0写到pc
	asm("str r0, [sp, #0x3c]");
	////====================================================================
	asm("LDMFD sp!, {R0}");
	asm("msr cpsr, r0");
	//自己需要的栈平掉pop
	asm("LDMFD sp!, {R0-R12,LR,PC}");
}


int main()
{
	//获得fopen函数地址->利用linux里的API(MAN手册)
	void *hand = dlopen("libc.so", RTLD_NOW);
	void *hook_addr = dlsym(hand, "fopen");//得到的地址为奇数0xb6ef80e5
	//更改内存属性(MAN手册)
	//mprotect第一个参数必须页边界对齐，最后12位地址必须为0，再改整个页属性
	mprotect((void*)((int)hook_addr & 0xfffff000), 0x1000, PROT_READ | PROT_EXEC | PROT_WRITE);
	printf("%p\n", hook_addr);
	//从fopen地址跳到裸函数
	//真实地址：要减1 -> 0001B0E4； ldr读pc地址(hook_addr-1)->正好是下一条指令地址
	*(uint32_t *)((uint32_t)hook_addr - 1) = 0xf000f8df;  //LDR PC,[PC] -> DF F8 00 F0
	//LDR PC,[PC](THUMB) thumb模式下读PC要+4字节;  [pc] -> +4地址的内存（为nakefun地址）给pc;
	//＋4个字节为裸函数地址,即跳转到目标地址
	*(uint32_t *)((uint32_t)hook_addr - 1 + 4) = (uint32_t)nakeFun;
	//返回地址->需要跳转回来的地址(ARM->THUMB 标志位改变 +1)->0001B0EC+1->0001B0ED
	ret_addr = (uint32_t)hook_addr - 1 + 8 + 1;
	while (1)
	{
		FILE *fp = fopen("/data/user/android_server", "rb");//android_server调试服务器-elf文件
		uint32_t data;
		fread(&data, 4, 1, fp);// 读前4个字节->elf签名
		fclose(fp);
		printf("data: %08x\n", data);
		printf("arg0: %s\n", arg0);
		printf("arg1: %s\n", arg1);
		getchar();
	}
	return 0;
}
