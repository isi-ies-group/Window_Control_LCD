#ifndef SPA_FUNC_H
#define SPA_FUNC_H

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

void SPA_f();
int getTimezone(int year, int month, int day);
void printTimeDecimal(double time);
int getTimezoneForCountryName(const char *country, int year, int month, int day);

#ifdef __cplusplus
}

int getTimezoneForCountry(const std::string& country, int year, int month, int day);
#endif

#endif /* SPA_FUNC_H */
