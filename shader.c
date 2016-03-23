#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <GL/glew.h>

void arb_info(GLhandleARB h) {
  GLint len, llen;
  GLchar *info = 0;
  glGetObjectParameterivARB(h, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
  if (len > 0) {
    info = malloc(len * sizeof(GLchar));
    glGetInfoLogARB(h, len, &llen, info);
    fprintf(stderr, "%s\n",info);
    free(info);
  }
}

static char * map_file(const GLchar *fname, GLint *len) {
  struct stat buf;
  int fd = -1;
  void *prg = 0;
  if (stat(fname, &buf) != 0) goto error;
  fd = open(fname, 0);
  if (fd != 0) goto error;
  prg = mmap(0, buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (prg <= 0) goto error;
  if (len) *len = buf.st_size;
  return prg;
error:
  perror(fname);
  return 0;
}

GLhandleARB load_shaders(const GLchar *fname, GLenum type) {
  GLchar *vs = NULL;
  const GLchar * vv = vs;
  GLint vl = 0;
  GLhandleARB handle;

  handle = glCreateShaderObjectARB(type);

  vs = map_file(fname, &vl);
  glShaderSourceARB(handle, 1, &vv, &vl);
  munmap(vs, vl);

  glCompileShaderARB(handle);
  arb_info(handle);
  return handle;
}

