/*
 * JOS file system format
 */

// We don't actually want to define off_t!
#define off_t xxx_off_t
#define bool xxx_bool
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#undef off_t
#undef bool

// Prevent inc/types.h, included from inc/fs.h,
// from attempting to redefine types defined in the host's inttypes.h.
#define JOS_INC_TYPES_H
// Typedef the types that inc/mmu.h needs.
typedef uint32_t physaddr_t;
typedef uint32_t off_t;
typedef int bool;

#include <inc/mmu.h>
#include <inc/fs.h>

#define ROUNDUP(n, v) ((n) - 1 + (v) - ((n) - 1) % (v))
#define MAX_DIR_ENTS 128

struct Dir
{
	struct File *f;
	struct File *ents;
	int n;
};

uint32_t nblocks;
char *diskmap, *diskpos;
struct Super *super;
uint32_t *bitmap;

void
panic(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
	abort();
}

void
readn(int f, void *out, size_t n)
{
	size_t p = 0;
	while (p < n) {
		int m = read(f, out + p, n - p);
		if (m < 0)
			panic("read: %s", strerror(errno));
		if (m == 0)
			panic("read: Unexpected EOF");
		p += m;
	}
}

uint32_t
blockof(void *pos)
{
	return ((char*)pos - diskmap) / BLKSIZE;
}

void *
alloc(uint32_t bytes)
{
	void *start = diskpos;
	diskpos += ROUNDUP(bytes, BLKSIZE);
	if (blockof(diskpos) >= nblocks)
		panic("out of disk blocks");
	return start;
}

void
opendisk(const char *name)
{
	int r, diskfd, nbitblocks;

	// открываем файл, переданный в argv[1] в качестве диска
	if ((diskfd = open(name, O_RDWR | O_CREAT, 0666)) < 0)
		panic("open %s: %s", name, strerror(errno));

	// сначала обрезаем файл до 0, затем устанавливаем ему нужный размер. 
	// свободное место заполнится нулями
	if ((r = ftruncate(diskfd, 0)) < 0 || (r = ftruncate(diskfd, nblocks * BLKSIZE)) < 0)
		panic("truncate %s: %s", name, strerror(errno));

	/*
		void * mmap(void *start, size_t length, int prot , int flags, int fd, off_t offset);

		Функция mmap отражает length байтов, начиная со смещения offset файла (или другого объекта), 
		определенного файловым описателем fd, в память, начиная с адреса start. 
		Последний параметр (адрес) необязателен, и обычно бывает равен 0. 
		Настоящее местоположение отраженных данных возвращается самой функцией mmap, 
		и никогда не бывает равным 0.

		Аргумент prot описывает желаемый режим защиты памяти 

		MAP_SHARED - Разделить использование этого отражения с другими процессами, 
		отражающими тот же объект. Запись информации в эту область памяти будет эквивалентна
		записи в файл. Файл может не обновляться до вызова функций msync(2) или munmap(2).

		Подробнее https://www.opennet.ru/man.shtml?topic=mmap&category=2&russian=0
	*/

	// копируем содержимое файла в diskmap
	if ((diskmap = mmap(NULL, nblocks * BLKSIZE, PROT_READ|PROT_WRITE,
			    		MAP_SHARED, diskfd, 0)) == MAP_FAILED)
		panic("mmap %s: %s", name, strerror(errno));

	// закрываем скопированный файл
	close(diskfd);

	// устанавливаем указатель в начало диска
	diskpos = diskmap;
	// Нулевой блок будет как NULL
	alloc(BLKSIZE);
	// выделяем второй блок под super и заполняем его
	super = alloc(BLKSIZE);
	super->s_magic = FS_MAGIC;
	super->s_nblocks = nblocks;
	super->s_root.f_type = FTYPE_DIR;
	strcpy(super->s_root.f_name, "/");

	/* в bitmap каждый бит будет использоваться для отображения занятости блока */

	// считаем, сколько блоков нам нужно аллоцировать для bitmap
	nbitblocks = (nblocks + BLKBITSIZE - 1) / BLKBITSIZE;
	bitmap = alloc(nbitblocks * BLKSIZE);

	// заполняем все биты в bitmap единичками - типа свободны
	memset(bitmap, 0xFF, nbitblocks * BLKSIZE);
}

void
finishdisk(void)
{
	int r, i;

	// для всех занятых блоков (они идут в начале диска) устанавливаем нули в bitmap
	for (i = 0; i < blockof(diskpos); ++i)
		bitmap[i/32] &= ~(1<<(i%32));

	if ((r = msync(diskmap, nblocks * BLKSIZE, MS_SYNC)) < 0)
		panic("msync: %s", strerror(errno));
}

void
finishfile(struct File *f, uint32_t start, uint32_t len)
{
	int i;
	f->f_size = len;
	len = ROUNDUP(len, BLKSIZE);
	for (i = 0; i < len / BLKSIZE && i < NDIRECT; ++i)
		f->f_direct[i] = start + i;
	if (i == NDIRECT) {
		uint32_t *ind = alloc(BLKSIZE);
		f->f_indirect = blockof(ind);
		for (; i < len / BLKSIZE; ++i)
			ind[i - NDIRECT] = start + i;
	}
}

// настраиваем *dout на работу с файлом f как с папкой 
void
startdir(struct File *f, struct Dir *dout)
{
	dout->f = f;
	dout->ents = malloc(MAX_DIR_ENTS * sizeof *dout->ents);
	dout->n = 0;
}

struct File *
diradd(struct Dir *d, uint32_t type, const char *name)
{
	struct File *out = &d->ents[d->n++];
	if (d->n > MAX_DIR_ENTS)
		panic("too many directory entries");
	strcpy(out->f_name, name);
	out->f_type = type;
	return out;
}

void
finishdir(struct Dir *d)
{
	int size = d->n * sizeof(struct File);
	struct File *start = alloc(size);
	memmove(start, d->ents, size);
	finishfile(d->f, blockof(start), ROUNDUP(size, BLKSIZE));
	free(d->ents);
	d->ents = NULL;
}

void
writefile(struct Dir *dir, const char *name)
{
	int r, fd;
	struct File *f;
	struct stat st;
	const char *last;
	char *start;

	// открываем файл на чтение
	if ((fd = open(name, O_RDONLY)) < 0)
		panic("open %s: %s", name, strerror(errno));
	// получаем Stat об этом файле в st
	if ((r = fstat(fd, &st)) < 0)
		panic("stat %s: %s", name, strerror(errno));
	// проверяем, что файл регулярный (то есть не папка и не что-то еще)
	if (!S_ISREG(st.st_mode))
		panic("%s is not a regular file", name);
	// проверяем, что файл не слишком большой
	if (st.st_size >= MAXFILESIZE)
		panic("%s too large", name);

	// получаем указатель на _последнее_ вхождение символа '/' в name или NULL, если '/' в name нет
	last = strrchr(name, '/');
	// устанавливаем last на финальный элемент адреса /.../.../.../elem
	if (last)
		last++;
	else
		last = name;

	// добавляем в папку dir регулярный файл с именем last
	f = diradd(dir, FTYPE_REG, last);
	// выделяем на диске столько места, сколько нужно добавленному файлу
	start = alloc(st.st_size);
	// считываем все содержимое файла с дескриптером fd на "диск" 
	readn(fd, start, st.st_size);
	// заполняем файл f информацией: адреса его блоков, начиная со start, и его размер
	finishfile(f, blockof(start), st.st_size);
	close(fd);
}

void
usage(void)
{
	fprintf(stderr, "Usage: fsformat fs.img NBLOCKS files...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int i;
	char *s;
	struct Dir root;

	assert(BLKSIZE % sizeof(struct File) == 0);

	if (argc < 3)
		usage();

	/* long strtol( const char *str, char **str_end, int base ); */
	/* Если base == 0, то основание системы счисления определяется автоматически */
	nblocks = strtol(argv[2], &s, 0);
	/* проверяем, что в argv[2] передется число и ничего больше */
	if (*s || s == argv[2] || nblocks < 2 || nblocks > 1024)
		usage();

	// копируем содержимое файла в *diskmap, настраиваем Super, bitmap и diskpos
	opendisk(argv[1]);

	/* 	&super->s_root - имеет тип File*			 */
	startdir(&super->s_root, &root);
	for (i = 3; i < argc; i++)
		writefile(&root, argv[i]);
	finishdir(&root);

	finishdisk();
	return 0;
}

