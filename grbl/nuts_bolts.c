/*
 nuts_bolts.c - Shared functions
 Part of Grbl

 Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
 Copyright (c) 2009-2011 Simen Svale Skogsrud

 Grbl is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Grbl is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "grbl.h"

#define MAX_INT_DIGITS 8 // Maximum number of digits in int32 (and float)

uint8_t str_length(char* line)
{
    uint8_t length = 0;
    while (line[length] && length < LINE_BUFFER_SIZE)
    {
        length++;
    }

    return length;
}

// do not handle overflow
uint8_t read_dec_uint(char* line, unsigned int* value)
{
    char *ptr = line;
    uint8_t digits = 0;

    value = 0;

    while (*ptr && digits < MAX_INT_DIGITS)
    {
        uint8_t chr = *ptr++;
        if ((chr >= '0') && (chr <= '9'))
        {
            chr -= '0';
            *value = *value * 10 + chr;
            digits ++;
        }
        else
        {
            break;
        }
    }

    return digits;
}

// Extracts a floating point value from a string. The following code is based loosely on
// the avr-libc strtod() function by Michael Stumpf and Dmitry Xmelkov and many freely
// available conversion method examples, but has been highly optimized for Grbl. For known
// CNC applications, the typical decimal value is expected to be in the range of E0 to E-4.
// Scientific notation is officially not supported by g-code, and the 'E' character may
// be a g-code word on some CNC systems. So, 'E' notation will not be recognized.
// NOTE: Thanks to Radu-Eosif Mihailescu for identifying the issues with using strtod().
uint8_t read_float(char *line, uint8_t *char_counter, float *float_ptr)
{
    char *ptr = line + *char_counter;
    unsigned char c;

    // Grab first character and increment pointer. No spaces assumed in line.
    c = *ptr++;

    // Capture initial positive/minus character
    bool isnegative = false;
    if (c == '-')
    {
        isnegative = true;
        c = *ptr++;
    }
    else if (c == '+')
    {
        c = *ptr++;
    }

    // Extract number into fast integer. Track decimal in terms of exponent value.
    uint32_t intval = 0;
    int8_t exp = 0;
    uint8_t ndigit = 0;
    bool isdecimal = false;
    while (1)
    {
        c -= '0';
        if (c <= 9)
        {
            ndigit++;
            if (ndigit <= MAX_INT_DIGITS)
            {
                if (isdecimal)
                {
                    exp--;
                }
                intval = (((intval << 2) + intval) << 1) + c; // intval*10 + c
            }
            else
            {
                if (!(isdecimal))
                {
                    exp++;
                }  // Drop overflow digits
            }
        }
        else if (c == (('.' - '0') & 0xff) && !(isdecimal))
        {
            isdecimal = true;
        }
        else
        {
            break;
        }
        c = *ptr++;
    }

    // Return if no digits have been read.
    if (!ndigit)
    {
        return (false);
    };

    // Convert integer into floating point.
    float fval;
    fval = (float) intval;

    // Apply decimal. Should perform no more than two floating point multiplications for the
    // expected range of E0 to E-4.
    if (fval != 0)
    {
        while (exp <= -2)
        {
            fval *= 0.01;
            exp += 2;
        }
        if (exp < 0)
        {
            fval *= 0.1;
        }
        else if (exp > 0)
        {
            do
            {
                fval *= 10.0;
            }
            while (--exp > 0);
        }
    }

    // Assign floating point value with correct sign.
    if (isnegative)
    {
        *float_ptr = -fval;
    }
    else
    {
        *float_ptr = fval;
    }

    *char_counter = ptr - line - 1; // Set char_counter to next statement

    return (true);
}

// Non-blocking delay function used for general operation and suspend features.
void delay_sec(float seconds, uint8_t mode)
{
    uint16_t i = ceil(1000 / DWELL_TIME_STEP * seconds);
    while (i-- > 0)
    {
        if (sys.abort)
        {
            return;
        }
        if (mode == DELAY_MODE_DWELL)
        {
            protocol_execute_realtime();
        }
        else
        { // DELAY_MODE_SYS_SUSPEND
          // Execute rt_system() only to avoid nesting suspend loops.
            protocol_exec_rt_system();
            if (sys.suspend & SUSPEND_RESTART_RETRACT)
            {
                return;
            } // Bail, if safety door reopens.
        }
        _delay_ms(DWELL_TIME_STEP); // Delay DWELL_TIME_STEP increment
    }
}

// Delays variable defined milliseconds. Compiler compatibility fix for _delay_ms(),
// which only accepts constants in future compiler releases.
void delay_ms(uint16_t ms)
{
    while (ms--)
    {
        _delay_ms(1);
    }
}

// Delays variable defined microseconds. Compiler compatibility fix for _delay_us(),
// which only accepts constants in future compiler releases. Written to perform more
// efficiently with larger delays, as the counter adds parasitic time in each iteration.
void delay_us(uint32_t us)
{
    while (us)
    {
        if (us < 10)
        {
            _delay_us(1);
            us--;
        }
        else if (us < 100)
        {
            _delay_us(10);
            us -= 10;
        }
        else if (us < 1000)
        {
            _delay_us(100);
            us -= 100;
        }
        else
        {
            _delay_ms(1);
            us -= 1000;
        }
    }
}

// Simple hypotenuse computation function.
float hypot_f(float x, float y)
{
    return (sqrt(x * x + y * y));
}

float convert_delta_vector_to_unit_vector(float *vector)
{
    uint8_t idx;
    float magnitude = 0.0;
    for (idx = 0; idx < N_AXIS; idx++)
    {
        if (vector[idx] != 0.0)
        {
            magnitude += vector[idx] * vector[idx];
        }
    }
    magnitude = sqrt(magnitude);
    float inv_magnitude = 1.0 / magnitude;
    for (idx = 0; idx < N_AXIS; idx++)
    {
        vector[idx] *= inv_magnitude;
    }
    return (magnitude);
}

float limit_value_by_axis_maximum(float *max_value, float *unit_vec)
{
    uint8_t idx;
    float limit_value = SOME_LARGE_VALUE;
    for (idx = 0; idx < N_AXIS; idx++)
    {
        if (unit_vec[idx] != 0)
        {  // Avoid divide by zero.
            limit_value = min(limit_value, fabs(max_value[idx] / unit_vec[idx]));
        }
    }
    return (limit_value);
}
