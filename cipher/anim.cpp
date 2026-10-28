// Cipher - springs in closed form (anim.hpp).
#include "anim.hpp"
#include <cmath>

namespace cipher
{
    bool Spring::Step(float dt, const Motion& m)
    {
        if (Resting())
            return false;
        if (!(dt > 0.0f))
            return true;
        // x'' + 2 z w x' + w^2 x = 0 around the target, solved exactly over dt: the same path at any frame rate
        const double w = 2.0 * 3.14159265358979323846 / (double)(m.response > 1e-4f ? m.response : 1e-4f);
        const double z = m.damping > 0.0f ? (double)m.damping : 0.0;
        const double x0 = (double)value - (double)target, v0 = (double)velocity, t = (double)dt;
        double x = 0.0, v = 0.0;
        if (z < 0.999)
        {
            const double wd = w * std::sqrt(1.0 - z * z);
            const double e = std::exp(-z * w * t), c = std::cos(wd * t), s = std::sin(wd * t);
            const double b = (v0 + z * w * x0) / wd;
            x = e * (x0 * c + b * s);
            v = -z * w * x + e * (b * wd * c - x0 * wd * s);
        }
        else if (z <= 1.001)
        {
            const double e = std::exp(-w * t), b = v0 + w * x0;
            x = (x0 + b * t) * e;
            v = (v0 - w * b * t) * e;
        }
        else
        {
            const double r = std::sqrt(z * z - 1.0);
            const double r1 = -w * (z - r), r2 = -w * (z + r);
            const double c2 = (v0 - r1 * x0) / (r2 - r1), c1 = x0 - c2;
            const double e1 = std::exp(r1 * t), e2 = std::exp(r2 * t);
            x = c1 * e1 + c2 * e2;
            v = c1 * r1 * e1 + c2 * r2 * e2;
        }
        // at rest once the motion is below what any pixel or color level shows
        const double scale = std::fabs((double)target) > 1.0 ? std::fabs((double)target) : 1.0;
        if (std::fabs(x) < 1e-4 * scale && std::fabs(v) < 1e-3 * scale)
        {
            value = target;
            velocity = 0.0f;
            return false;
        }
        value = (float)((double)target + x);
        velocity = (float)v;
        return true;
    }

    float EaseOutCubic(float t)
    {
        t = Clamp01(t);
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }

    float EaseInOutCubic(float t)
    {
        t = Clamp01(t);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    }

    float EaseOutBack(float t, float overshoot)
    {
        t = Clamp01(t);
        const float c3 = overshoot + 1.0f, u = t - 1.0f;
        return 1.0f + c3 * u * u * u + overshoot * u * u;
    }

    float Smoothstep(float a, float b, float x)
    {
        const float t = Clamp01((x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }
}
