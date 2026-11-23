#include <BK370Carriage.h>
void setup() { bk370::begin<0>(); }
void loop() { bk370::tick<0>(); }
