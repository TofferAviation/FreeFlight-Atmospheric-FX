#!/usr/bin/env python3
"""Make the v6.0 shader loader self-contained on the Windows SDK runner."""
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "src" / "ContrailVolumeRenderer.h"
text = path.read_text(encoding="utf-8")
text = text.replace('#  include <GL/glext.h>\n', '')

marker = '#endif\n\n#include <algorithm>'
types = '''#ifndef GL_VERTEX_SHADER
#  define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#  define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#  define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#  define GL_LINK_STATUS 0x8B82
#endif

#if IBM
using FFGLCreateShaderProc = GLuint (APIENTRY *)(GLenum);
using FFGLShaderSourceProc = void (APIENTRY *)(GLuint, GLsizei, const char* const*, const GLint*);
using FFGLCompileShaderProc = void (APIENTRY *)(GLuint);
using FFGLGetShaderivProc = void (APIENTRY *)(GLuint, GLenum, GLint*);
using FFGLGetShaderInfoLogProc = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
using FFGLCreateProgramProc = GLuint (APIENTRY *)(void);
using FFGLAttachShaderProc = void (APIENTRY *)(GLuint, GLuint);
using FFGLLinkProgramProc = void (APIENTRY *)(GLuint);
using FFGLGetProgramivProc = void (APIENTRY *)(GLuint, GLenum, GLint*);
using FFGLGetProgramInfoLogProc = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
using FFGLUseProgramProc = void (APIENTRY *)(GLuint);
using FFGLDeleteProgramProc = void (APIENTRY *)(GLuint);
using FFGLDeleteShaderProc = void (APIENTRY *)(GLuint);
using FFGLGetUniformLocationProc = GLint (APIENTRY *)(GLuint, const char*);
using FFGLUniform1fProc = void (APIENTRY *)(GLint, GLfloat);
using FFGLUniform3fProc = void (APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat);
#endif

#endif

#include <algorithm>'''
if marker not in text:
    raise RuntimeError("OpenGL include marker was not found")
text = text.replace(marker, types, 1)

replacements = {
    'PFNGLCREATESHADERPROC': 'FFGLCreateShaderProc',
    'PFNGLSHADERSOURCEPROC': 'FFGLShaderSourceProc',
    'PFNGLCOMPILESHADERPROC': 'FFGLCompileShaderProc',
    'PFNGLGETSHADERIVPROC': 'FFGLGetShaderivProc',
    'PFNGLGETSHADERINFOLOGPROC': 'FFGLGetShaderInfoLogProc',
    'PFNGLCREATEPROGRAMPROC': 'FFGLCreateProgramProc',
    'PFNGLATTACHSHADERPROC': 'FFGLAttachShaderProc',
    'PFNGLLINKPROGRAMPROC': 'FFGLLinkProgramProc',
    'PFNGLGETPROGRAMIVPROC': 'FFGLGetProgramivProc',
    'PFNGLGETPROGRAMINFOLOGPROC': 'FFGLGetProgramInfoLogProc',
    'PFNGLUSEPROGRAMPROC': 'FFGLUseProgramProc',
    'PFNGLDELETEPROGRAMPROC': 'FFGLDeleteProgramProc',
    'PFNGLDELETESHADERPROC': 'FFGLDeleteShaderProc',
    'PFNGLGETUNIFORMLOCATIONPROC': 'FFGLGetUniformLocationProc',
    'PFNGLUNIFORM1FPROC': 'FFGLUniform1fProc',
    'PFNGLUNIFORM3FPROC': 'FFGLUniform3fProc',
}
for old, new in replacements.items():
    text = text.replace(old, new)

if 'glext.h' in text or 'PFNGL' in text:
    raise RuntimeError("External OpenGL extension typedef dependency remains")
path.write_text(text, encoding="utf-8", newline="\n")
print("Applied self-contained Windows OpenGL shader loader")
