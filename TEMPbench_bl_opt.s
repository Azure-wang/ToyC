.section .data
.section .text
.globl main
.weak _start
.globl _start
_start:
  call main
  li a7, 93
  ecall
.globl main
main:
  addi sp, sp, -32
  sw s0, 28(sp)
  sw s1, 24(sp)
  sw s2, 20(sp)
  addi s0, sp, 32
.L_body_main_1:
  li a0, 0
  mv s1, a0
  mv a0, s1
  mv s2, a0
.L_while_cond_2:
  mv a0, s1
  mv t0, a0
  li a0, 200000
  blt t0, a0, .L_while_body_3
  j .L_while_end_4
.L_while_body_3:
  mv a0, s2
  mv t0, a0
  mv a0, s1
  mv t2, a0
  li a0, 3
  mul a0, t2, a0
  addi a0, a0, 1
  mv t1, a0
  li a0, 97
  rem a0, t1, a0
  add a0, t0, a0
  mv s2, a0
  mv a0, s1
  addi a0, a0, 1
  mv s1, a0
  j .L_while_cond_2
.L_while_end_4:
  mv a0, s2
.L_ret_main_0:
  lw s2, -12(s0)
  lw s1, -8(s0)
  lw s0, -4(s0)
  addi sp, sp, 32
  ret
