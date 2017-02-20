#ifndef _MYALLOC_H_
#define _MYALLOC_H_
#include <stdint.h>

/*
 * ��ʼ���ڴ������
 * totalsize	��ʹ���ڴ��С
 * maxsize		���η����ڴ�����С
 * return		0-�ɹ�������ʧ��
 */
extern int myalloc_init(uint32_t totalsize, uint32_t maxsize);
/*
 * �ͷ��ڴ������ʹ�õ���Դ
 */
extern void myalloc_fini();
/*
 * �����ڴ�
 * size			�������ڴ��С
 * return		�ɹ��򷵻��ڴ�ָ�룬���򷵻�NULL
 */
extern void* myalloc_alloc(uint32_t size);
/*
 * �ͷ��ڴ�
 * ptr			���ͷ��ڴ�ָ��
 */
extern void myalloc_free(void* ptr);
/*
 * ȡ�ڴ������ͳ��ָ��
 * size			ʣ���ʹ���ڴ��С
 */
extern void myalloc_stat(uint64_t* size);

#endif
