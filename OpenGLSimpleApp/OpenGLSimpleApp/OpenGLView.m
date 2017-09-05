//
//  OpenGLView.m
//  OpenGLSimpleApp
//
//  Created by karek314 on 04/09/2017.
//  Copyright © 2017 karek314. All rights reserved.
//

#import "OpenGLView.h"
#include <OpenGL/gl.h>

@implementation OpenGLView


- (void) loop{
    //Simple loop simulation at one frame per second
    [self setNeedsDisplay:YES];
}


-(void) drawRect: (NSRect) bounds {
    float i = arc4random_uniform(100);
    i = i/100;
    glClearColor(i, 0, i, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    NSLog(@"Main function called");
    drawTriangle();
    CGLContextObj ctx = 0;
    CGLFlushDrawable(ctx);
    glFlush();
    [self performSelector:@selector(loop) withObject:nil afterDelay:1.0];
}


static void drawTriangle () {
    glColor3f(1.0f, 0.1f, 0.1f);
    glBegin(GL_TRIANGLES);
    {
        glVertex3f(  0.0,  0.5, 0.0);
        glVertex3f( -0.2, -0.2, 0.0);
        glVertex3f(  0.2, -0.2 ,0.0);
        glVertex3f(  0.2, -0.2 ,0.0);
    }
    glEnd();
}


@end
