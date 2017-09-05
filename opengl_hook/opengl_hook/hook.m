//
//  hook.c
//  opengl_hook
//
//  Created by karek314 on 04/09/2017.
//  Copyright © 2017 karek314. All rights reserved.
//

#include "hook.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <OpenGL/gl.h>
#include <math.h>
#include "mach_override.h"


static void drawRectangle () {
    printf("Addtionaly drawing rectangle!");
    glColor3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        glVertex3f(-0.5f, 0.5f, 0.0f);
        glVertex3f( 0.5f, 0.5f, 0.0f);
        glVertex3f( 0.5f,-0.5f, 0.0f);
        glVertex3f(-0.5f,-0.5f, 0.0f);
    }
    glEnd();
}


void (*Original_CGLFlushDrawable)(CGLContextObj ctx);


void CGLFlushDrawableOverride(CGLContextObj ctx) {
    drawRectangle();
    printf("\nCalling hooked CGLFlushDrawable!\n");
    Original_CGLFlushDrawable(ctx);
}


__attribute__ ((visibility("default")))
__attribute__((constructor))
void OverlayEntryPoint(){
    printf("\nhooking opengl flush drawable\n");
    if (mach_override_ptr(dlsym(RTLD_DEFAULT, "CGLFlushDrawable"), CGLFlushDrawableOverride, (void **)&Original_CGLFlushDrawable) != 0)
        printf("\nCGLFlushDrawable hook failed\n");
    else
        printf("Hooked successfully!\n");
    
}

