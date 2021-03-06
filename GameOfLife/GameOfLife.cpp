#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
//#define __NO_STD_VECTOR // Use cl::vector instead of STL version
#define __CL_ENABLE_EXCEPTIONS

#include <iostream>
#include <fstream>
#include <cmath>
#include <OpenCL/opencl.h>
#include "cl.hpp"

#include <sstream>


#include <GLUT/glut.h>
#include <stdlib.h>

#define PLATFORM_ID 0
#define DEVICE_ID 0

//#include "Common.h"

using namespace std;

cl::Context context;
cl::CommandQueue queue;
cl::Program simulationProgram;

cl::Kernel stepKernel;

cl::Buffer visualizationBufferGPU;
GLuint texture;
unsigned char *visualizationBufferCPU;

int windowWidth = 600;
int windowHeight = 600;
float zoom = 3;
int fieldWidth = 600;
int fieldHeight = 600;
float offsetW = 0;
float offsetH = 0;

bool keysPressed[256];
int mX, mY;


void randomizeField() {
    for (int i = 0; i < fieldHeight * fieldWidth; i++) {
        visualizationBufferCPU[i * 4 + 0] = 0;
        visualizationBufferCPU[i * 4 + 1] = 0;
        visualizationBufferCPU[i * 4 + 2] = 0;
        visualizationBufferCPU[i * 4 + 3] = (rand() % 5) * 255;
    }

    queue.enqueueWriteBuffer(visualizationBufferGPU, CL_TRUE, 0,
                             sizeof(unsigned char) * 4 * fieldWidth * fieldHeight,
                             visualizationBufferCPU, NULL, NULL);
}

void initOpenGL() {
    glGenTextures(1, &texture);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    visualizationBufferCPU = new unsigned char[fieldHeight * fieldWidth * 4];
}

void simulationStep() {
    try {
        // copy
        auto buffer = cl::Buffer(context, CL_MEM_READ_ONLY,
                                 sizeof(unsigned char) * 4 * fieldWidth * fieldHeight,
                                 nullptr, nullptr);
        queue.enqueueWriteBuffer(buffer, CL_TRUE, 0,
                                 sizeof(unsigned char) * 4 * fieldWidth * fieldHeight,
                                 visualizationBufferCPU, NULL, NULL);

        // enque
        stepKernel.setArg(2, buffer);
        cl::NDRange global((size_t) (fieldWidth * fieldHeight));
        queue.enqueueNDRangeKernel(stepKernel, cl::NullRange, global, cl::NullRange);

        // read back
        queue.enqueueReadBuffer(visualizationBufferGPU, CL_TRUE, 0,
                                sizeof(unsigned char) * 4 * fieldWidth * fieldHeight,
                                visualizationBufferCPU, NULL, NULL);

        // finish
        queue.finish();
    } catch (cl::Error err) {
        std::cout << "Error: " << err.what() << "(" << err.err() << ")" << std::endl;
        exit(3);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fieldWidth, fieldHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 visualizationBufferCPU);
}

void visualizationStep() {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    float ONEW = float(windowWidth) / float(fieldWidth) / zoom;
    float ONEH = float(windowHeight) / float(fieldHeight) / zoom;

    float STARTW = (ONEW * (1 - offsetW)) / -2;
    float ENDW = (ONEW * (1 + offsetW)) / 2;
    float STARTH = (ONEH * (1 - offsetH)) / -2;
    float ENDH = (ONEH * (1 + offsetH)) / 2;

    glBegin(GL_QUADS);
    glTexCoord2f(STARTW, STARTH);
    glVertex2f(-1, -1);

    glTexCoord2f(ENDW, STARTH);
    glVertex2f(1, -1);

    glTexCoord2f(ENDW, ENDH);
    glVertex2f(1, 1);

    glTexCoord2f(STARTW, ENDH);
    glVertex2f(-1, 1);
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

void display() {
    visualizationStep();
    glutSwapBuffers();
}

void idle() {
    glutPostRedisplay();
}

void reshape(int newWidth, int newHeight) {
    windowWidth = newWidth;
    windowHeight = newHeight;


    glViewport(0, 0, newWidth, newHeight);
}

void keyDown(unsigned char key, int x, int y) {
    keysPressed[key] = true;
}

void keyUp(unsigned char key, int x, int y) {
    keysPressed[key] = false;
    switch (key) {
        case 27:
            exit(0);
            break;
        case 'q':
            zoom *= 1.1f;
            offsetW *= 1.1f;
            offsetH *= 1.1f;
            break;
        case 'e':
            zoom /= 1.1f;
            offsetW /= 1.1f;
            offsetH /= 1.1f;
            break;
        case '0':
            zoom = 3;
            break;

        case 'w':
            offsetH += 0.1f;
            break;
        case 'a':
            offsetW -= 0.1f;
            break;
        case 's':
            offsetH -= 0.1f;
            break;
        case 'd':
            offsetW += 0.1f;
            break;

        case 'r':
            randomizeField();
            display();
            break;
        default:
            simulationStep();
    }
    glutPostRedisplay();
}

void mouseClick(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        mX = x;
        mY = y;
    }
}

void mouseMove(int x, int y) {
    offsetW -= float(x - mX) / windowWidth;
    offsetH += float(y - mY) / windowHeight;

    mX = x;
    mY = y;
}

void initSimulation() {
    // source: http://stackoverflow.com/questions/26517114/how-to-compile-opencl-project-with-kernels

    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);

        std::vector<cl::Device> devices;
        platforms[PLATFORM_ID].getDevices(CL_DEVICE_TYPE_CPU | CL_DEVICE_TYPE_GPU, &devices);

        context = cl::Context(devices);
        queue = cl::CommandQueue(context, devices[DEVICE_ID]);

        std::ifstream sourceFile{"kernels/programs.cl"};
        std::string sourceCode(std::istreambuf_iterator<char>(sourceFile), (std::istreambuf_iterator<char>()));
        cl::Program::Sources source(1, std::make_pair(sourceCode.c_str(), sourceCode.length()));

        simulationProgram = cl::Program(context, source);
        simulationProgram.build(devices);

        visualizationBufferGPU = cl::Buffer(context, CL_MEM_WRITE_ONLY,
                                            sizeof(unsigned char) * 4 * fieldWidth * fieldHeight,
                                            nullptr, nullptr);
        randomizeField();

        stepKernel = cl::Kernel(simulationProgram, "tick");
        stepKernel.setArg(0, fieldWidth);
        stepKernel.setArg(1, fieldHeight);
        stepKernel.setArg(3, visualizationBufferGPU);
    } catch (cl::Error err) {
        std::cout << "Error: " << err.what() << "(" << err.err() << ")" << std::endl;
        exit(2);
    }
}

int main(int argc, char *argv[]) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize(windowWidth, windowHeight);
    glutCreateWindow("GPGPU Game of Life");

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    initOpenGL();

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutMouseFunc(mouseClick);
    glutMotionFunc(mouseMove);

    // OpenCL processing
    initSimulation();

    glutMainLoop();
    return (0);
}
