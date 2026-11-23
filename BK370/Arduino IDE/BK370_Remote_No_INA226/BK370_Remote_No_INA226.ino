#include <BK370Carriage.h>

void setup() { bk370::begin<2, false>(); }
void loop() { bk370::tick<2, false>(); }
