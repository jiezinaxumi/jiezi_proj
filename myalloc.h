#ifndef _MYALLOC_H_
#define _MYALLOC_H_
#include <stdint.h>

/*
 * 初始化内存分配器
 * totalsize	总使用内存大小
 * maxsize		单次分配内存最大大小
 * return		0-成功，否则失败
 */
extern int myalloc_init(uint32_t totalsize, uint32_t maxsize);
/*
 * 释放内存分配器使用的资源
 */
extern void myalloc_fini();
/*
 * 分配内存
 * size			待分配内存大小
 * return		成功则返回内存指针，否则返回NULL
 */
extern void* myalloc_alloc(uint32_t size);
/*
 * 释放内存
 * ptr			待释放内存指针
 */
extern void myalloc_free(void* ptr);
/*
 * 取内存分配器统计指标
 * size			剩余可使用内存大小
 */
extern void myalloc_stat(uint64_t* size);

#endif
