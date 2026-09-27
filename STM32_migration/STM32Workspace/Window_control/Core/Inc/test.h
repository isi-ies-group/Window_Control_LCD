#ifndef TEST_H
#define TEST_H

#include <stdbool.h>

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

int dummy(int x);

void Test_SetAutoModeInputs(float pan,
                            float tilt,
                            bool tilt_correction,
                            float longitude,
                            float latitude,
                            const char *country);

void Test_ApplyStartupParameters(void);

#ifdef __cplusplus
}

void autoModeInputs(float pan,
                    float tilt,
                    bool tilt_correction,
                    float longitude,
                    float latitude,
                    const std::string &country);
#endif


#endif
