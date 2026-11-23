#include <BK370Carriage.h>
void setup() { bk370::begin<2>(); }
void loop() { bk370::tick<2>(); }
