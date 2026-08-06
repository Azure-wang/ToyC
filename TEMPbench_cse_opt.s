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
  addi sp, sp, -48
  sw s0, 44(sp)
  sw s1, 40(sp)
  sw s2, 36(sp)
  sw s3, 32(sp)
  sw s4, 28(sp)
  sw s5, 24(sp)
  addi s0, sp, 48
.L_body_main_1:
  li a0, 0
  mv s1, a0
  mv a0, s1
  mv s2, a0
.L_while_cond_2:
  mv a0, s1
  mv t0, a0
  li a0, 80000
  blt t0, a0, .L_while_body_3
  j .L_while_end_4
.L_while_body_3:
  mv a0, s1
  mv t0, a0
  li a0, 97
  rem a0, t0, a0
  mv s3, a0
  mv a0, s1
  mv t0, a0
  li a0, 31
  rem a0, t0, a0
  mv s4, a0
  mv a0, s3
  mv t0, a0
  mv a0, s4
  add a0, t0, a0
  mv s5, a0
  mv a0, s2
  mv t1, a0
  mv a0, s5
  mv t2, a0
  mv a0, s5
  mul a0, t2, a0
  add a0, t1, a0
  mv t0, a0
  mv a0, s5
  add a0, t0, a0
  mv s2, a0
  mv a0, s1
  addi a0, a0, 1
  mv s1, a0
  j .L_while_cond_2
.L_while_end_4:
  mv a0, s2
.L_ret_main_0:
  lw s5, -24(s0)
  lw s4, -20(s0)
  lw s3, -16(s0)
  lw s2, -12(s0)
  lw s1, -8(s0)
  lw s0, -4(s0)
  addi sp, sp, 48
  ret
