#include <inc/lib.h>
#include <inc/time.h>

#define CRT_ROWS 25
#define CRT_COLS 80

void
clear_scr(void)
{
	int i;

	for (i = 0; i < CRT_ROWS; i++)
	{
		cputchar('\n');
	}

	for (i = 0; i < CRT_ROWS * CRT_COLS; i++)
	{
		cputchar('\b');
	}
}

int
auth(const char *login, const char *password, bool clear)
{
	int fd, r, r0;
	char passwd_record[BUFSIZE * PASSWD_MEMBERS_NUM];
	char shadow_record[BUFSIZE * SHADOW_MEMBERS_NUM];
	char buf[BUFSIZE];
	struct Passwd passwd;
	struct Shadow shadow;

	if ((fd = open("/etc/passwd", O_RDONLY)) < 0)
	{
		return fd;
	}
	
	if ((r0 = find_record(fd, login, passwd_record,
		PASSWD_MEMBERS_NUM)) < 0)
	{
		return r0;
	}

	close(fd);

	if ((fd = open("/etc/shadow", O_RDONLY)) < 0)
	{
		return fd;
	}


	if ((r = find_record(fd, login, shadow_record,
		SHADOW_MEMBERS_NUM)) < 0)
	{
		return r;
	}

	close(fd);

	if ((r == 0 && r0 > 0) || (r > 0 && r0 == 0))
	{
		return -1;
	}

	if (r == 0 && r0 == 0)
	{
		return 0;
	}

	if ((r = parse_into_passwd(passwd_record, &passwd)) < 0)
	{
		return r;
	}

	if ((r = parse_into_shadow(shadow_record, &shadow)) < 0)
	{
		return r;
	}

	crypt(password, shadow.user_salt, buf);

	if (!strncmp(buf, shadow.user_hash, BUFSIZE))
	{
		if ((r = chdir(passwd.user_path)) < 0)
		{
			return r;
		}

		if (clear)
		{
			clear_scr();
		}

		if ((r = spawnl(passwd.user_shell, passwd.user_shell + 1,
			(char *) 0)) < 0)
		{
			return r;
		}

		wait(r);

		if (clear)
		{
			clear_scr();
		}

		exit();
	}

	return 1;
}

void 
prompt(char *login, char *password)
{
	char *buf;

	if (login[0] == '\0')
	{
		buf = readline("login: ");
		strncpy(login, buf, BUFSIZE);
		login[BUFSIZE - 1] = '\0';
	}

	buf = readline_no_echo("password: ");
	strncpy(password, buf, BUFSIZE);
	password[BUFSIZE - 1] = '\0';
}

void
usage(void)
{
	cprintf("Usage: login [-c] [name]\n");
	exit();
}

void
umain(int argc, char *argv[])
{
	int i, r, now;
	bool clear;
	char login[BUFSIZE], password[BUFSIZE];
	struct Argstate args;

	for (i = 0; i < BUFSIZE; i++)
	{
		login[i] = '\0';
		password[i] = '\0';
	}

	clear = false;
	argstart(&argc, argv, &args);

	while ((r = argnext(&args)) >= 0)
	{
		switch(r)
		{
			case 'c':
				clear = true;
				break;
			default:
				usage();
				break;
		}
	}

	if (argc > 2)
	{
		usage();
	}
	else if (argc == 2)
	{
		strncpy(login, argv[1], BUFSIZE);
	}

	prompt(login, password);

	if ((r = auth(login, password, clear)) < 0)
	{
		panic("login: auth: %i", r);
	}
	else if (r >= 0)
	{
		now = vsys_gettime();
		cprintf("Login incorrect\n\n");

		while (vsys_gettime() - now <= 1)
		{
		}

		exit();
	}
}
