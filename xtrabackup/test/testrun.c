/*
 * testrun.c - a parallel test runner for the XtraBackup test suite
 */
/* BEGIN LICENSE
 * Copyright (C) 2012 Percona Inc.
 *
 * Written by Stewart Smith
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 * END LICENSE */

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/select.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>

#include <signal.h>

struct testcase
{
  char *name;
  char *buf;
  size_t end_of_buf;
  size_t bufsz;
};

pid_t *childpid;
int nchildpid=0;

static void kill_children(int sig)
{
  int i;
  int status;

  (void)sig;

  fprintf(stderr, "Killing child processes...\n");

  for(i=0; i<nchildpid; i++)
    if(childpid[i] > 0)
    {
      kill(childpid[i], SIGKILL);
      wait(&status);
    }

  exit(EXIT_SUCCESS);
}

int collect_testcases_filter(const struct dirent *a)
{
  int l;

  if (a->d_name[0] == '.')
    return 0;

  l= strlen(a->d_name);

  if (l > 2 && strncmp(a->d_name + l - 3, ".sh", 3)==0)
    return 1;

  return 0;
}

static int collect_testcases(const char* suitedir, struct testcase **cases)
{
  struct dirent **namelist;
  int n;
  int i;

  n= scandir(suitedir, &namelist, collect_testcases_filter, alphasort);

  *cases= (struct testcase*) malloc(sizeof(struct testcase)*n);

  for(i=0; i<n; i++)
  {
    (*cases)[i].name= strdup(namelist[i]->d_name);
    (*cases)[i].buf= NULL;
    (*cases)[i].end_of_buf= 0;
    (*cases)[i].bufsz= 0;
    free(namelist[i]);
  }

  free(namelist);

  return n;
}

static void free_testcases(struct testcase *cases, int n)
{
  while (n>0)
  {
    free(cases[--n].name);
  }
  free(cases);
}

static int run_testcase_in_child(int nr, struct testcase *t, pid_t *cpid, const char* xbtarget)
{
  int fd[2];

  printf("[%d] LAUNCHING - %s\n", nr, t->name);

  if (pipe(fd) == -1)
  {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  *cpid= fork();
  if (*cpid == 0)
  {
    /* child */
    close(fd[0]);

    char tname[500];
    snprintf(tname, sizeof(tname), "t/%s",t->name);

    char basedir[PATH_MAX];
    char cwd[PATH_MAX];
    snprintf(basedir, sizeof(basedir), "%s/var/%d", getcwd(cwd,sizeof(cwd)), nr);

    mkdir("var",0700);
    mkdir(basedir,0700);

    char logname[PATH_MAX];
    snprintf(logname, sizeof(logname), "%s/var/%d.log", getcwd(cwd,sizeof(cwd)), nr);

    int logfd= open(logname, O_WRONLY|O_APPEND|O_CREAT, 0600);
    dup2(logfd, STDOUT_FILENO);
    dup2(logfd, STDERR_FILENO);
    close(logfd);

    char subunitfd[50];
    snprintf(subunitfd, sizeof(subunitfd), "/dev/fd/%d", fd[1]);

    char* xbtarget_param;
    if (xbtarget)
        xbtarget_param= strdup(xbtarget);
    else
        xbtarget_param= NULL; 

    char *const newargv[] = {"testrun.sh", "-n",
		       "-t", tname,
		       "-b", basedir,
		       "-r", subunitfd,
		       (xbtarget)? "-c" : NULL, xbtarget_param,
		       NULL };
    char *newenviron[] = { NULL };
    execve(newargv[0], newargv, newenviron);
    perror("execve");
    exit(EXIT_FAILURE);
  }
  else
  {
    /* parent */
    close(fd[1]);
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    return fd[0];
  }
}

static inline void subunit_progress_sign(int fd, int n, char *sign)
{
  char *buf;
  const char* fmt= "progress: %s%d\n";
  size_t sz= 1+strlen(fmt)+100;
  size_t l;

  buf= (char*)malloc(sz);

  l= snprintf(buf, sz, fmt, sign, n);
  assert(l < sz);

  write(fd, buf, l);

  free(buf);
}

static void run_testcases(struct testcase *testcases, int nrcases,
			  int njobs, int timeout, const char* xbtarget)
{
  int childfd[njobs];
  int nfds= 0;
  int retval;
  pid_t chpid[njobs];
  int status;
  int next_testcase= 0;
  int i;
  fd_set rfds;
  fd_set efds;
  struct timeval tv;
  int nchildren;
  int childtest[njobs];

  int subunitfd= open("test_results.subunit", O_TRUNC|O_WRONLY|O_APPEND|O_CREAT, 0600);
  subunit_progress_sign(subunitfd, nrcases, "");

  if (nrcases < njobs)
    njobs= nrcases;

  childpid= chpid;
  nchildpid= njobs;

  for(i=0; i<njobs; i++)
  {
    childtest[i]=next_testcase++;
    childfd[i]= run_testcase_in_child(i, &testcases[childtest[i]], &childpid[i], xbtarget);
  }

  fflush(stdout);

loop:
  FD_ZERO(&efds);
  FD_ZERO(&rfds);

  nchildren=0;

  for (i=0; i<njobs; i++)
  {
    if (childfd[i] != -1)
    {
      FD_SET(childfd[i], &efds);
      FD_SET(childfd[i], &rfds);
      nfds= (childfd[i] > nfds)? childfd[i] : nfds;
      nchildren++;
    }
  }

  tv.tv_sec= timeout;
  tv.tv_usec= 0;

  retval = select(nfds+1, &rfds, NULL, &efds, &tv);

  if (retval == -1)
    perror("select()");
  else if (retval)
  {
    int childexited=0;

    for (i=0; i<njobs; i++)
    {
      if(childfd[i] != -1
	 && (FD_ISSET(childfd[i], &efds) || FD_ISSET(childfd[i], &rfds)))
      {
	ssize_t r=0;

	do {
	  struct testcase *t= &testcases[childtest[i]];

	  if(t->bufsz == t->end_of_buf)
	  {
	    t->bufsz+=4000;
	    t->buf= (char*)realloc(t->buf, t->bufsz);
	  }

	  r= read(childfd[i], t->buf+t->end_of_buf, t->bufsz - t->end_of_buf);
	  if (r>0)
	    t->end_of_buf+= r;
	} while(r>0);

	pid_t waited= waitpid(childpid[i], &status, WNOHANG);
	if (!(WIFEXITED(status) || WIFSIGNALED(status)))
	  continue;

	if (waited != childpid[i])
	  continue;

	write(subunitfd, testcases[childtest[i]].buf,
	      testcases[childtest[i]].end_of_buf);

	close(childfd[i]);
	printf("[%d] completed %s status %d\n",
	       i, testcases[childtest[i]].name, WEXITSTATUS(status));
	childfd[i]=-1;
	nchildren--;

	if (next_testcase < nrcases)
	{
	  childtest[i]=next_testcase++;
	  childfd[i]= run_testcase_in_child(i, &testcases[childtest[i]], &childpid[i], xbtarget);
	  nfds= (childfd[i] > nfds)? childfd[i] : nfds;
	  nchildren++;
	}
	printf("\nnrchildren= %d, %d tests remaining\n",
	       nchildren, nrcases-next_testcase);
	childexited=1;
	fflush(stdout);
      }
    }
    if (childexited)
    {
      printf ("Running: ");
      for(i=0; i<njobs; i++)
	if (childfd[i] != -1)
	  printf("%s ",testcases[childtest[i]].name);
      printf("\n");
    }
  }
  else
  {
    printf("Timeout\n");
    kill_children(SIGKILL);
    exit(EXIT_FAILURE);
  }

  if (nchildren==0)
    goto end;

  goto loop;

end:

  close(subunitfd);
  return;
}

int main(int argc, char* argv[])
{
  const char* suitedir= "t/";
  int njobs= 4;
  int opt;
  int nrcases;
  struct testcase *testcases;
  int timeout= 600;
  const char* xbtarget= NULL;

  struct sigaction sa;

  sa.sa_flags= 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = kill_children;
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);

#ifdef  _SC_NPROCESSORS_ONLN
  njobs= sysconf(_SC_NPROCESSORS_ONLN) /2;
#endif

#ifdef _SC_PHYS_PAGES
  long nrpages= sysconf(_SC_PHYS_PAGES);
  long pagesize= sysconf(_SC_PAGESIZE);
  long pages_per_job= (128*(1 << 20)) / pagesize;
  nrpages= nrpages/2;
  if ((pages_per_job * njobs) > nrpages)
    njobs= nrpages / pages_per_job;
#endif

  if (njobs == 0)
    njobs= 1;

  while ((opt = getopt(argc, argv, "j:s:t:c:")) != -1)
  {
    switch (opt) {
    case 'c':
      xbtarget= optarg;
      break;
    case 's':
      suitedir= optarg;
      break;
    case 'j':
      njobs= atoi(optarg);
      break;
    case 't':
      timeout= atoi(optarg);
      break;
    default:
      fprintf(stderr, "Usage: %s [-s suite] [-j parallel] [-t timeout]\n",
              argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  printf("%s running with: -s %s -j %d -t %d\n\n",
	 argv[0], suitedir,njobs, timeout);

  nrcases= collect_testcases(suitedir, &testcases);

  printf("Found %d testcases\n", nrcases);

  run_testcases(testcases, nrcases, njobs, timeout, xbtarget);

  free_testcases(testcases, nrcases);

  return 0;
}
