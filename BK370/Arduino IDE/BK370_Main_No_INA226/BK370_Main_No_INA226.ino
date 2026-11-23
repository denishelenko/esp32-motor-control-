#include <BK370Carriage.h>

void setup() { bk370::begin<1, false>(); }
void loop() { bk370::tick<1, false>(); }
