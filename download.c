#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "service.h"
#include "mimetype.h"

/* demoҵ��ģ�� 
 * ʵ�ֹ��ܣ� 
 * ʵ�ּ򵥵�http get���󣬸���get���ļ�·���������ļ����ݣ���cache����
 * �����
 * docroot: �ļ���Ŀ¼
 * dlspeed: �������ʣ�KB/S
 */

/* ����һ���Լ�����־����mylog���������֪ͨ����Ϊupdate_mylog */
DECLARE_LOG(mylog, update_mylog)

#define MAX_URL_LEN	2048
/* һЩҵ�����ò��� */
static char docroot[128];
static uint32_t dlspeed;
static uint32_t cache_timeout;
static uint32_t expire_timeout;
static int keepalive = 0;

static int magic = 0;

/* ����һ���̷߳�Χ�Ļ����� */
__thread unsigned char* filedata = NULL;

/* ����������Լ��
 * ����>0��ʾ����������ұ�ʾ������ĳ��ȣ�==0��ʾ����δ������<0��ʾ���ݴ���
 */
static int check_request(struct conn* c, char* data, int len) {
	/* ������Զ�c�����user��Ա���з����ڴ��д��ҵ������ */	
	if(len < 14)
		return 0;
	
	data[len] = '\0';			
	if(!strncmp(data, "GET /", 5)) {
		char* p;
		if((p = strstr(data + 5, "\r\n\r\n")) != NULL) {
			char* q;
			int len;
			if((q = strstr(data + 5, " HTTP/")) != NULL) {
				len = q - data - 5;
				if(len < MAX_URL_LEN) {
					strncpy((char*)c->user, data + 5, len);	
					((char*)c->user)[len] = '\0';
					return p - data + 4;
				}
			}
			return -2;	
		}
		else
			return 0;
	}
	else
		return -1;

}
/* ����һ������ */
static int handle_request(struct conn* c, const char* reqdata, int reqlen) {
	/* ������Զ�c��user��Ա����ҵ���߼����� */
	
	char httpheader[1024] = {0};
	char filename[128] = {0};
	int ret;		
	unsigned char key[16];
	uint32_t filelen = 0;
	char* p = (char*)c->user;
	char cc = *p;
	while((cc = *p) && (cc != '?') && (cc != '&')) ++p;
	if(cc)
		*p = '\0';
					
	sprintf(filename, "%s/%s", docroot, (char*)c->user);
	LOG(mylog, LOG_DEBUG, "file = %s\n", filename);
/*
	if(strstr(filename, ".dat")) {
		if(magic == 0) {
			magic = 1;
			LOG(mylog, LOG_DEBUG, "pending to proc 10000\n");
			return R_PROC_PENDING(10000);
		}
		else {
			magic = 0;
		}
	}
*/
	getmd5(filename, strlen(filename), key);
	ret = mycache_get(key, filedata, &filelen);	
	if(!ret) {		//cache������û����
		sprintf(httpheader, "HTTP/1.1 200 OK\r\nConnection: %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: maxage=%u\r\n\r\n", keepalive ? "Keep-Alive" : "Close", getmimetype(filename), filelen, expire_timeout);
		SEND_2_CLIENT(c, httpheader, strlen(httpheader), ret);
		SEND_2_CLIENT(c, (const char*)filedata, filelen, ret);
		LOG(mylog, LOG_NORMAL, "hit file = %s\n", filename);
	}
	else {			//cache�����л��߹���

		int fd;
		struct stat64 st;
		fd = open64(filename, O_RDONLY);
		if(fd > 0) {
			fstat64(fd, &st);
			
			//����http header
			sprintf(httpheader, "HTTP/1.1 200 OK\r\nConnection: %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: maxage=%u\r\n\r\n", keepalive ? "Keep-Alive" : "Close", getmimetype(filename), (unsigned)st.st_size, expire_timeout);
			SEND_2_CLIENT(c, httpheader, strlen(httpheader), ret);
			
			//����http body
			if(st.st_size > maxcachesize) {	//�������cache��С��ʹ��sendfile�����ļ�����
				SENDFILE_2_CLIENT(c, fd, 0, (uint32_t)st.st_size, ret); 	
				LOG(mylog, LOG_NORMAL, "big file = %s, dont cache\n", filename);
			}
			else {							//���ļ����ݶ����ڴ棬����cache���������ڴ�����
				filelen = st.st_size;
				lseek(fd, 0, SEEK_SET);
				read(fd, filedata, filelen);	
				close(fd);
				
				SEND_2_CLIENT(c, (const char*)filedata, filelen, ret);
				mycache_set(key, filedata, filelen, cache_timeout);
				LOG(mylog, LOG_NORMAL, "small file = %s, len = %d, cache it\n", filename, filelen);
			}

		}
		else {
			sprintf(httpheader, "HTTP/1.1 403 File Not Found\r\n\r\n");	
			SEND_2_CLIENT(c, httpheader, strlen(httpheader), ret);
			LOG(mylog, LOG_NORMAL, "no file = %s\n", filename);
		}
	}

	return R_COMP;
}


int svc_ginit(struct context* ct) {

	/* ע��һ���Լ�����־����mylog����������������Ҫ��DECLARE_LOG�Ĳ���һ�� */	
	char* logname = myconfig_get_value("svc_log_name");
	int loglevel = LOG_DEBUG;
	if(myconfig_get_value("svc_log_level"))
		loglevel = getloglevel(myconfig_get_value("svc_log_level"));
	int rotatesize = myconfig_get_intval("svc_log_rotate_size", 10);
	int maxlognum = myconfig_get_intval("svc_max_log_num", 10);
	REGISTER_LOG(logname == NULL ? "../log/download.log" : logname, loglevel, (rotatesize << 20), 0, maxlognum, mylog, update_mylog);

	/* �������ļ���ȡһЩҵ�����ò��� */
	dlspeed = myconfig_get_intval("svc_dlspeed", 0);	
	char* p;
	if((p = myconfig_get_value("svc_docroot")) != NULL)
		strcpy(docroot, p);
	else
		strcpy(docroot, "./");
	
	keepalive = myconfig_get_intval("svc_keepalive", 0);
	cache_timeout = myconfig_get_intval("svc_cache_timeout", 3600);
	expire_timeout = myconfig_get_intval("svc_expire_timeout", 86400);
	
	initmimetype();
		
	LOG(mylog, LOG_DEBUG, "download ginit succ\n");
	return R_SUCC;
}
int svc_linit(struct context* ct) {
	/* ����һ���̷߳�Χ�Ļ��������洢�ļ����� */
	filedata = (unsigned char*)malloc(maxcachesize);
	if(filedata)
		return R_SUCC;
	else
		return R_FAIL;	
}
int svc_initconn(struct conn* c, struct context* ct) {
	LOG(mylog, LOG_DEBUG, "download initconn\n");
	/* ���ӿ�ʼʱ����һЩҵ��Ҫʹ�õ��ڴ� */
	/* ���������url���ڴ�ռ� */
	c->speed = dlspeed;
	c->user = malloc(MAX_URL_LEN);
	if(c->user)	
		return R_SUCC;
	else
		return R_FAIL;
}
int svc_recv(struct conn* c, struct context* ct) {
	LOG(mylog, LOG_DEBUG, "download recv\n");
	int ret;
	char* data;
	uint32_t len;
	int n;
	int x = R_CONT; 
	int offset = 0;
	RECV_FROM_CLIENT(c, data, len, ret);
	
	LOG(mylog, LOG_DEBUG, "svc_recv, len=%u\n", len);
	if(!ret) {
		/* ����ѭ����������Ϊ����һ�ν��յ����ݰ����˶������ʵ��������������ٳ��� */
		while(offset < len) {
			LOG(mylog, LOG_DEBUG, "check data, offset=%u, len=%u\n", offset, len - offset);
			n = check_request(c, data + offset, len - offset);
			if(n > 0) {
				x = handle_request(c, data + offset, n);
				
				if((x & 0xFFFF) != R_PENDING)
					USE_FROM_CLIENT(c, n);
				offset += n;
			}
			else if(n == 0)
				break;
			else {
				x = R_FAIL;
				break;
			}
		}
	}
	return x;
}
int svc_send(struct conn* c, struct context* ct) {
	LOG(mylog, LOG_DEBUG, "download send\n");
	if(keepalive)
		return R_SUCC;
	else	
		return R_CLOSE;  //���ﷵ��R_CLOSE��ʾ���������ݺ�ر����ӣ����Ҫ����������Ӧ�÷���R_SUCC
}
/*int svc_send_once(struct conn* c, struct context* ct) {
	LOG(mylog, LOG_NORMAL, "send_once, len=%u\n", ct->send_size);
	return R_SUCC;
}*/
void svc_finiconn(struct conn* c, struct context* ct) {
	LOG(mylog, LOG_DEBUG, "download finiconn\n");
	/* ���ӹر�ʱ���ͷ�ҵ�������ڴ� */
	if(c->user) {
		free(c->user);
		c->user = NULL;
	}
}
void svc_lfini(struct context* ct) {
	/* �ͷ��̷߳�Χ�Ļ����� */
	if(filedata)
		free(filedata);
}	
