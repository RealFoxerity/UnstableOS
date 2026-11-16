// made by my dear fwiend Adrian :3
// go show him some love https://github.com/AdrUlb <3

#include <stdlib.h>

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

#include <float.h>

static const long double pow10s_exact[] = {
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
	1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
	1e20, 1e21, 1e22
};

static const long double pow10_exp[] = {
	1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256, 1e512L, 1e1024L, 1e2048L, 1e4096L
};

long double strtold(const char *restrict nptr, char **restrict endptr)
{
	const char *p = nptr;

	while (isspace((unsigned char)*p))
		p++;

	bool negative = false;
	if (*p == '+')
	{
		p++;
	}
	else if (*p == '-')
	{
		negative = true;
		p++;
	}

	if (strncasecmp(p, "nan", 3) == 0)
	{
		if (endptr)
			*endptr = (char *)(p + 3);

		return negative ? -NAN : NAN;
	}

	if (strncasecmp(p, "infinity", 8) == 0)
	{
		if (endptr)
			*endptr = (char *)(p + 8);

		return negative ? -INFINITY : INFINITY;
	}

	if (strncasecmp(p, "inf", 3) == 0)
	{
		if (endptr)
			*endptr = (char *)(p + 3);

		return negative ? -INFINITY : INFINITY;
	}

	bool has_digits = false;
	bool has_decimal = false;
	uint64_t mantissa = 0;
	int32_t exponent = 0;
	while (*p)
	{
		if (!isdigit((unsigned char)*p))
		{
			if (has_decimal || *p != '.')
				break;

			has_decimal = true;
			p++;
			continue;
		}

		has_digits = true;

		if (mantissa < (UINT64_MAX / 10)) // Can we accommodate this many digits?
		{
			mantissa = mantissa * 10 + (*p - '0');
			if (has_decimal)
				exponent--;
		}
		else // Dropped digits are compensated for by increasing the exponent
		{
			if (!has_decimal)
				exponent++;
		}

		p++;
	}

	if (!has_digits)
	{
		if (endptr)
			*endptr = (char *)nptr;

		return 0.0;
	}

	if (*p == 'e' || *p == 'E')
	{
		const char *exponent_start = p;
		p++;

		bool exponent_negative = false;
		if (*p == '+')
		{
			p++;
		}
		else if (*p == '-')
		{
			exponent_negative = true;
			p++;
		}

		int32_t exponent_explicit = 0;
		bool exponent_has_digits = false;
		while (isdigit((unsigned char)*p))
		{
			exponent_has_digits = true;
			if (exponent_explicit < 100000)
				exponent_explicit = exponent_explicit * 10 + (*p - '0');

			p++;
		}

		if (!exponent_has_digits) // Turns out 'e' wasn't the start of an exponent
		{
			p = exponent_start;
		}
		else if (exponent_negative)
		{
			exponent -= exponent_explicit;
		}
		else
		{
			exponent += exponent_explicit;
		}
	}

	if (endptr)
		*endptr = (char *)p;

	if (mantissa == 0)
		return negative ? -0.0 : 0.0;

	long double result = (long double)mantissa;

	// Max safe integer when represented as a double precision float
	//const uint64_t MAX_SAFE_INT = 1ULL << LDBL_MANT_DIG;
	const uint64_t MAX_SAFE_INT = -1ULL;

	if (mantissa <= MAX_SAFE_INT && exponent >= -22 && exponent <= 22)
	{
		// We're in luck: we can use precomputed doubles, we'll be as exact as technically possible and can perform this in O(1) :3
		if (exponent < 0)
		{
			result /= pow10s_exact[-exponent];
		}
		else
		{
			result *= pow10s_exact[exponent];
		}
	}
	else
	{
		// Unlucky: we can't use precomputed doubles, takes longer and may be slightly inaccurate
		// FIXME: this should use some kind of bigint library to be more accurate

		if (exponent > LDBL_MAX_10_EXP)
			return negative ? -INFINITY : INFINITY;

		if (exponent < LDBL_MIN_10_EXP)
			return negative ? -0.0 : 0.0;

		bool exponent_negative = exponent < 0;
		int32_t exponent_absolute = exponent_negative ? -exponent : exponent;

		long double scale = 1.0L;
		int bit = 0;

		while (exponent_absolute > 0 && bit < 9)
		{
			if (exponent_absolute & 1)
				scale *= pow10_exp[bit];

			exponent_absolute >>= 1;
			bit++;
		}

		if (exponent_negative)
		{
			result /= scale;
		}
		else
		{
			result *= scale;
		}
	}

	return negative ? -result : result;
}

double strtod(const char *restrict nptr, char **restrict endptr) {
	return (double)strtold(nptr, endptr);
}
float strtof(const char *restrict nptr, char **restrict endptr) {
	return (float)strtod(nptr, endptr);
}

#include <errno.h>

double atof(const char *str) {
	int old_errno = ___get_errno();
	double ret = strtod(str, NULL);
	___set_errno(old_errno);
	return ret;
}