#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <vector>
#include <iostream>
#include <cmath>
#include <string>

using namespace std;

const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;

struct Cubie {
    glm::ivec3 gridPos;      // логическая позиция: -1, 0, 1
    glm::mat4 orientation;   // накопленная ориентация малого кубика
};

enum class Axis { X, Y, Z };

struct MoveAnimation {
    bool active = false;
    Axis axis = Axis::X;
    int layer = 0;          // -1,0,1
    int dir = 1;            // +1 or -1
    float angle = 0.0f;     // текущий угол
    float speed = 180.0f;   // градусов/сек
};

vector<Cubie> cubies;
MoveAnimation anim;

int selectedLayer = 1; // 0->-1, 1->0, 2->1

GLuint shaderProgram;
GLuint VAO, VBO;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

float globalRotX = 25.0f;
float globalRotY = 35.0f;

bool leftMousePressed = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

// -------- ШЕЙДЕРЫ --------
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 ourColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 ourColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(ourColor, 1.0);
}
)";

// -------- УТИЛИТЫ --------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        cerr << "Shader compile error:\n" << infoLog << endl;
    }
    return shader;
}

GLuint createShaderProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int success;
    char infoLog[512];
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(prog, 512, nullptr, infoLog);
        cerr << "Program link error:\n" << infoLog << endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

glm::vec3 getStickerColor(int face) {
    // 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z
    switch (face) {
    case 0: return { 1.0f, 0.0f, 0.0f };     // красный
    case 1: return { 1.0f, 0.5f, 0.0f };     // оранжевый
    case 2: return { 1.0f, 1.0f, 1.0f };     // белый
    case 3: return { 1.0f, 1.0f, 0.0f };     // жёлтый
    case 4: return { 0.0f, 0.7f, 0.0f };     // зелёный
    case 5: return { 0.0f, 0.3f, 1.0f };     // синий
    default: return { 0.1f, 0.1f, 0.1f };
    }
}

void buildCubeGeometry() {
    // Куб с цветными гранями
    // Каждая вершина: position (3) + color (3)
    vector<float> vertices;

    auto addFace = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 color) {
        // 2 треугольника
        vector<glm::vec3> faceVerts = { v0, v1, v2, v2, v3, v0 };
        for (auto& v : faceVerts) {
            vertices.push_back(v.x);
            vertices.push_back(v.y);
            vertices.push_back(v.z);
            vertices.push_back(color.r);
            vertices.push_back(color.g);
            vertices.push_back(color.b);
        }
    };

    float s = 0.45f;

    // +X
    addFace({ s,-s,-s }, { s, s,-s }, { s, s, s }, { s,-s, s }, getStickerColor(0));
    // -X
    addFace({ -s,-s, s }, { -s, s, s }, { -s, s,-s }, { -s,-s,-s }, getStickerColor(1));
    // +Y
    addFace({ -s, s,-s }, { -s, s, s }, { s, s, s }, { s, s,-s }, getStickerColor(2));
    // -Y
    addFace({ -s,-s, s }, { -s,-s,-s }, { s,-s,-s }, { s,-s, s }, getStickerColor(3));
    // +Z
    addFace({ -s,-s, s }, { s,-s, s }, { s, s, s }, { -s, s, s }, getStickerColor(4));
    // -Z
    addFace({ s,-s,-s }, { -s,-s,-s }, { -s, s,-s }, { s, s,-s }, getStickerColor(5));

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void initCubies() {
    cubies.clear();
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                Cubie c;
                c.gridPos = { x, y, z };
                c.orientation = glm::mat4(1.0f);
                cubies.push_back(c);
            }
        }
    }
}

bool cubieInLayer(const Cubie& c, Axis axis, int layer) {
    if (axis == Axis::X) return c.gridPos.x == layer;
    if (axis == Axis::Y) return c.gridPos.y == layer;
    return c.gridPos.z == layer;
}

glm::mat4 getAxisRotation(Axis axis, float angleDeg) {
    float rad = glm::radians(angleDeg);
    if (axis == Axis::X) return glm::rotate(glm::mat4(1.0f), rad, glm::vec3(1, 0, 0));
    if (axis == Axis::Y) return glm::rotate(glm::mat4(1.0f), rad, glm::vec3(0, 1, 0));
    return glm::rotate(glm::mat4(1.0f), rad, glm::vec3(0, 0, 1));
}

glm::ivec3 rotateGridPos90(glm::ivec3 p, Axis axis, int dir) {
    // dir = +1 or -1
    // Поворот на 90 градусов вокруг начала координат
    if (axis == Axis::X) {
        // y,z
        return (dir > 0) ? glm::ivec3(p.x, -p.z, p.y)
            : glm::ivec3(p.x, p.z, -p.y);
    }
    if (axis == Axis::Y) {
        // x,z
        return (dir > 0) ? glm::ivec3(p.z, p.y, -p.x)
            : glm::ivec3(-p.z, p.y, p.x);
    }
    // Axis::Z
    return (dir > 0) ? glm::ivec3(-p.y, p.x, p.z)
        : glm::ivec3(p.y, -p.x, p.z);
}

void finalizeMove() {
    for (auto& c : cubies) {
        if (cubieInLayer(c, anim.axis, anim.layer)) {
            c.gridPos = rotateGridPos90(c.gridPos, anim.axis, anim.dir);
            c.orientation = getAxisRotation(anim.axis, 90.0f * anim.dir) * c.orientation;
        }
    }
    anim.active = false;
    anim.angle = 0.0f;
}

void startMove(Axis axis, int layer, int dir) {
    if (anim.active) return;
    anim.active = true;
    anim.axis = axis;
    anim.layer = layer;
    anim.dir = dir;
    anim.angle = 0.0f;
}

int currentLayerValue() {
    return selectedLayer - 1; // 0->-1, 1->0, 2->1
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    static bool key1Pressed = false;
    static bool key2Pressed = false;
    static bool key3Pressed = false;
    static bool qPressed = false;
    static bool wPressed = false;
    static bool ePressed = false;

    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !key1Pressed) {
        selectedLayer = 0;
        key1Pressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE) key1Pressed = false;

    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !key2Pressed) {
        selectedLayer = 1;
        key2Pressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE) key2Pressed = false;

    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && !key3Pressed) {
        selectedLayer = 2;
        key3Pressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE) key3Pressed = false;

    int dir = shift ? -1 : 1;

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS && !qPressed) {
        startMove(Axis::X, currentLayerValue(), dir);
        qPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE) qPressed = false;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS && !wPressed) {
        startMove(Axis::Y, currentLayerValue(), dir);
        wPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_RELEASE) wPressed = false;

    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS && !ePressed) {
        startMove(Axis::Z, currentLayerValue(), dir);
        ePressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_RELEASE) ePressed = false;
}

void updateAnimation(float dt) {
    if (!anim.active) return;

    anim.angle += anim.speed * dt;
    if (anim.angle >= 90.0f) {
        finalizeMove();
    }
}

void drawCubie(const Cubie& c, const glm::mat4& view, const glm::mat4& projection) {
    glm::mat4 model = glm::mat4(1.0f);

    // Базовый сдвиг по сетке
    glm::vec3 pos = glm::vec3(c.gridPos) * 1.05f;

    // Временный поворот текущего слоя
    if (anim.active && cubieInLayer(c, anim.axis, anim.layer)) {
        glm::mat4 tempRot = getAxisRotation(anim.axis, anim.angle * anim.dir);
        model = glm::translate(model, pos);
        model = tempRot * model;
    }
    else {
        model = glm::translate(model, pos);
    }

    // Ориентация маленького кубика
    model = model * c.orientation;

    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
    GLint projLoc = glGetUniformLocation(shaderProgram, "projection");

    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            leftMousePressed = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        else if (action == GLFW_RELEASE) {
            leftMousePressed = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!leftMousePressed) return;

    double dx = xpos - lastMouseX;
    double dy = ypos - lastMouseY;

    lastMouseX = xpos;
    lastMouseY = ypos;

    float sensitivity = 0.4f;

    globalRotY += static_cast<float>(dx) * sensitivity;
    globalRotX += static_cast<float>(dy) * sensitivity;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Rubik Cube OpenGL", nullptr, nullptr);
    if (!window) {
        cerr << "Failed to create GLFW window" << endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    shaderProgram = createShaderProgram();
    buildCubeGeometry();
    initCubies();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);
        updateAnimation(deltaTime);

        // плавное общее вращение всей сцены
        globalRotY += 18.0f * deltaTime;
        globalRotX += 10.0f * deltaTime;

        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT,
            0.1f, 100.0f);

        glm::mat4 view = glm::mat4(1.0f);
        view = glm::translate(view, glm::vec3(0.0f, 0.0f, -10.0f));
        view = glm::rotate(view, glm::radians(globalRotX), glm::vec3(1, 0, 0));
        view = glm::rotate(view, glm::radians(globalRotY), glm::vec3(0, 1, 0));

        for (const auto& c : cubies) {
            drawCubie(c, view, projection);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        string title = "Rubik Cube OpenGL | Layer: " + to_string(currentLayerValue()) +
            " | Keys: 1/2/3 select layer, Q/W/E rotate, Shift reverse";
        glfwSetWindowTitle(window, title.c_str());
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}