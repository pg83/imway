// KMS-бэкенд: atomic modeset + dumb-буферы. Кадр рендерера (readback)
// копируется в scanout. Плюс libinput для мыши/клавиатуры.
#pragma once

struct Server;
struct Kms;
struct InputLinux;

// открывает DRM, выбирает коннектор/режим; выставляет server.out_w/out_h
Kms* kms_create(Server&, const char* dev_path);
// modeset + первый кадр; звать после инициализации рендерера
bool kms_start(Kms*);
void kms_present(Kms*, const void* pixels);
void kms_destroy(Kms*);

InputLinux* input_linux_create(Server&);
void input_linux_destroy(InputLinux*);
