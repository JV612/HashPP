OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x10000;
  
  .text : {
    *(.text*)
  }
  
  .data : {
    *(.data*)
  }
  
  .sdata : {
    __global_pointer$ = . + 0x800;
    *(.sdata*)
  }
  
  .bss : {
    *(.bss*)
  }
}
