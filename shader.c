#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <GL/glew.h>

void arb_info(GLhandleARB h) {
  GLint len = 0, llen = 0;
  GLchar *info = 0;
  glGetObjectParameterivARB(h, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
  if (len > 0) {
    info = malloc(len * sizeof(GLchar));
    glGetInfoLogARB(h, len, &llen, info);
    fprintf(stderr, "%s\n",info);
    free(info);
  }
}

static GLcharARB * read_file(const GLcharARB *fname, GLint *len) {
  struct stat buf;
  int fd = -1;
  GLcharARB *prg = 0;
  size_t bytes = 0;
  if (stat(fname, &buf) != 0) goto error;
  fd = open(fname, O_RDWR);
  if (fd < 0) goto error;
  prg = malloc(sizeof (GLcharARB) * buf.st_size + 1);
  bytes = read(fd, prg, buf.st_size);
  if (bytes < 0) goto error;
  if (len) *len = buf.st_size;
  prg[bytes] = '\0';
  close(fd);
  return prg;
error:
  perror(fname);
  exit(-2);
}

static GLcharARB * map_file(const GLcharARB *fname, GLint *len) {
  struct stat buf;
  int fd = -1;
  void *prg = 0;
  if (stat(fname, &buf) != 0) goto error;
  fd = open(fname, O_RDWR);
  if (fd < 0) goto error;
  prg = mmap(0, buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (prg <= 0) goto error;
  if (len) *len = buf.st_size;
  return prg;
error:
  perror(fname);
  exit(-2);
}

GLhandleARB load_shaders(const GLchar *fname, GLenum type) {
  const GLcharARB *vs = NULL;
  GLint vl = 0;
  GLhandleARB handle;
  handle = glCreateShaderObjectARB(type);
  arb_info(handle);
  vs = read_file(fname, &vl);
  glShaderSourceARB(handle, 1, &vs, NULL);
  arb_info(handle);
  glCompileShaderARB(handle);
  arb_info(handle);
  munmap((void *)vs, vl);
  return handle;
}

