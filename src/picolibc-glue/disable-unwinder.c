/*
 * disable-unwinder.c
 *
 *  Created on: Mar 19, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

/* This prevents the linking of libgcc unwinder code */
void __aeabi_unwind_cpp_pr0(void);
void __aeabi_unwind_cpp_pr1(void);
void __aeabi_unwind_cpp_pr2(void);

void __aeabi_unwind_cpp_pr0(void)
{
};

void __aeabi_unwind_cpp_pr1(void)
{
};

void __aeabi_unwind_cpp_pr2(void)
{
};


