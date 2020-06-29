#include "kis_mypaint_surface.h"

#include <KoColorSpace.h>
#include <kis_sequential_iterator.h>
#include <KoColorConversions.h>
#include <kis_algebra_2d.h>
#include <kis_cross_device_color_picker.h>
#include <kis_node.h>
#include <QtMath>
#include <qmath.h>

using namespace std;

KisMyPaintSurface::KisMyPaintSurface(KisPainter *painter, KisPaintDeviceSP paintNode)
{
    m_painter = painter;
    m_imageDevice = paintNode;

    m_surface = new MyPaintSurfaceInternal();
    mypaint_surface_init(m_surface);
    m_surface->m_owner = this;

    m_surface->draw_dab = this->draw_dab;
    m_surface->get_color = this->get_color;
}

int KisMyPaintSurface::draw_dab(MyPaintSurface *self, float x, float y, float radius, float color_r, float color_g,
                                float color_b, float opaque, float hardness, float color_a,
                                float aspect_ratio, float angle, float lock_alpha, float colorize) {

    MyPaintSurfaceInternal *surface = static_cast<MyPaintSurfaceInternal*>(self);
    return surface->m_owner->drawDabImpl(self, x, y, radius, color_r, color_g,
             color_b, opaque, hardness, color_a,
            aspect_ratio, angle, lock_alpha,  colorize);
}

void KisMyPaintSurface::get_color(MyPaintSurface *self, float x, float y, float radius,
                            float * color_r, float * color_g, float * color_b, float * color_a) {

    MyPaintSurfaceInternal *surface = static_cast<MyPaintSurfaceInternal*>(self);
    surface->m_owner->getColorImpl(self, x, y, radius, color_r, color_g, color_b, color_a);
}


/*GIMP's draw_dab and get_color code*/
int KisMyPaintSurface::drawDabImpl(MyPaintSurface *self, float x, float y, float radius, float color_r, float color_g,
                                float color_b, float opaque, float hardness, float color_a,
                                float aspect_ratio, float angle, float lock_alpha, float colorize) {

    const float one_over_radius2 = 1.0f / (radius * radius);
    const double angle_rad = angle / 360 * 2 * M_PI;
    const float cs = cos(angle_rad);
    const float sn = sin(angle_rad);
    float normal_mode;
    float segment1_slope;
    float segment2_slope;
    float r_aa_start;

    hardness = CLAMP (hardness, 0.0f, 1.0f);
    segment1_slope = -(1.0f / hardness - 1.0f);
    segment2_slope = -hardness / (1.0f - hardness);
    aspect_ratio = max(1.0f, aspect_ratio);

    r_aa_start = radius - 1.0f;
    r_aa_start = max(r_aa_start, 0.0f);
    r_aa_start = (r_aa_start * r_aa_start) / aspect_ratio;

    normal_mode = opaque * (1.0f - colorize);
    colorize = opaque * colorize;

    const KoColorSpace *colorSpace = painter()->device()->colorSpace();
    const QPoint pt = QPoint(x - radius - 1, y - radius - 1);
    const QSize sz = QSize(2 * (radius+1), 2 * (radius+1));

    const QRect dabRectAligned = QRect(pt, sz);
    const QPointF center = QPointF(x, y);

    KisAlgebra2D::OuterCircle outer(center, radius);

    KisSequentialIterator it(painter()->device(), dabRectAligned);

    while(it.nextPixel()) {

        QPoint pt(it.x(), it.y());

        if(outer.fadeSq(pt) > 1.0f)
            continue;

        qreal rr, base_alpha, alpha, dst_alpha, r, g, b, a;
        qreal opacity;

        if (radius < 3.0) {
            rr = calculate_rr_antialiased (it.x(), it.y(), x, y, aspect_ratio, sn, cs, one_over_radius2, r_aa_start);
        }
        else {
            rr = calculate_rr (it.x(), it.y(), x, y, aspect_ratio, sn, cs, one_over_radius2);
        }

        base_alpha = calculate_alpha_for_rr (rr, hardness, segment1_slope, segment2_slope);
        alpha = base_alpha * normal_mode;

        b = it.rawData()[0]/(255.0f);
        g = it.rawData()[1]/(255.0f);
        r = it.rawData()[2]/(255.0f);
        dst_alpha = it.rawData()[3]/(255.0f);

        a = alpha * (color_a - dst_alpha) + dst_alpha;

        if (a > 0.0f) {

            float src_term = (alpha * color_a) / a;
            float dst_term = 1.0f - src_term;
            r = color_r * src_term + r * dst_term;
            g = color_g * src_term + g * dst_term;
            b = color_b * src_term + b * dst_term;
        }

        if (colorize > 0.0f && base_alpha > 0.0f) {

            alpha = base_alpha * colorize;
            a = alpha + dst_alpha - alpha * dst_alpha;

            if (a > 0.0f) {

                float pixel_h, pixel_s, pixel_l, out_h, out_s, out_l;
                float out_r = r, out_g = g, out_b = b;

                float src_term = alpha / a;
                float dst_term = 1.0f - src_term;

                RGBToHSL(color_r, color_g, color_b, &pixel_h, &pixel_s, &pixel_l);
                RGBToHSL(out_r, out_g, out_b, &out_h, &out_s, &out_l);

                out_h = pixel_h;
                out_s = pixel_s;

                HSLToRGB(out_h, out_s, out_l, &out_r, &out_g, &out_b);

                r = (float)out_r * src_term + r * dst_term;
                g = (float)out_g * src_term + g * dst_term;
                b = (float)out_b * src_term + b * dst_term;
            }
        }

        it.rawData()[0] = b*255;
        it.rawData()[1] = g*255;
        it.rawData()[2] = r*255;
        it.rawData()[3] = a*255;

    }

    painter()->addDirtyRect(dabRectAligned);
    return 1;
}

void KisMyPaintSurface::getColorImpl(MyPaintSurface *self, float x, float y, float radius,
                            float * color_r, float * color_g, float * color_b, float * color_a) {

    if (radius < 1.0f)
        radius = 1.0f;

    *color_r = 0.0f;
    *color_g = 0.0f;
    *color_b = 0.0f;
    *color_a = 0.0f;

    const KoColorSpace *colorSpace = painter()->device()->colorSpace();
    const QPoint pt = QPoint(x - radius, y - radius);
    const QSize sz = QSize(2 * radius, 2 * radius);

    const QRect dabRectAligned = QRect(pt, sz);
    const QPointF center = QPointF(x, y);
    KisAlgebra2D::OuterCircle outer(center, radius);

    const float one_over_radius2 = 1.0f / (radius * radius);
    float sum_weight = 0.0f;
    float sum_r = 0.0f;
    float sum_g = 0.0f;
    float sum_b = 0.0f;
    float sum_a = 0.0f;

    KisSequentialIterator it(m_imageDevice, dabRectAligned);
    //KisRandomAccessorSP im = m_node->paintDevice()->createRandomAccessorNG(x, y);

    while(it.nextPixel()) {

        QPointF pt(it.x(), it.y());

        if(outer.fadeSq(pt) > 1.0)
            continue;

        /* pixel_weight == a standard dab with hardness = 0.5, aspect_ratio = 1.0, and angle = 0.0 */
        float yy = (it.y() + 0.5f - y);
        float xx = (it.x() + 0.5f - x);

        float rr = (yy * yy + xx * xx) * one_over_radius2;
        float pixel_weight = 0.0f;
        if (rr <= 1.0f)
            pixel_weight = 1.0f - rr;

//        im->moveTo(it.x(), it.y());

        qreal r, g, b, a;

        b = it.rawData()[0]/(255.0f);
        g = it.rawData()[1]/(255.0f);
        r = it.rawData()[2]/(255.0f);
        a = it.rawData()[3]/(255.0f);

        sum_r += pixel_weight * r;
        sum_g += pixel_weight * g;
        sum_b += pixel_weight * b;
        sum_a += pixel_weight * a;
        sum_weight += pixel_weight;
    }

    if (sum_a > 0.0f && sum_weight > 0.0f) {

        sum_r /= sum_weight;
        sum_g /= sum_weight;
        sum_b /= sum_weight;
        sum_a /= sum_weight;

        sum_r /= sum_a;
        sum_g /= sum_a;
        sum_b /= sum_a;

        *color_r = CLAMP(sum_r, 0.0f, 1.0f);
        *color_g = CLAMP(sum_g, 0.0f, 1.0f);
        *color_b = CLAMP(sum_b, 0.0f, 1.0f);
        *color_a = CLAMP(sum_a, 0.0f, 1.0f);
    }
}

KisPainter* KisMyPaintSurface::painter() {
    return m_painter;
}

MyPaintSurface* KisMyPaintSurface::surface() {
    return m_surface;
}

/*mypaint code*/
qreal KisMyPaintSurface::calculateOpacity(float angle, float hardness, float opaque, float x, float y,
                                        float xp, float yp, float aspect_ratio, float radius) {

    qreal cs = cos(angle/360*2*M_PI);
    qreal sn = sin(angle/360*2*M_PI);

    qreal dx = xp - x;
    qreal dy = yp - y;
    qreal dyr = (dy*cs-dx*sn)*aspect_ratio;
    qreal dxr = (dy*sn+dx*cs);
    qreal dd = (dyr*dyr + dxr*dxr) / (radius*radius);
    qreal opa;

    if (dd > 1)
        opa = 0;
    else if (dd < hardness)
        opa = dd + 1-(dd/hardness);
    else
        opa = hardness/(1-hardness)*(1-dd);

    qreal pixel_opacity = opa * opaque;
    return pixel_opacity;
}

inline float KisMyPaintSurface::calculate_rr (int  xp,
              int   yp,
              float x,
              float y,
              float aspect_ratio,
              float sn,
              float cs,
              float one_over_radius2) {

    const float yy = (yp + 0.5f - y);
    const float xx = (xp + 0.5f - x);
    const float yyr=(yy*cs-xx*sn)*aspect_ratio;
    const float xxr=yy*sn+xx*cs;
    const float rr = (yyr*yyr + xxr*xxr) * one_over_radius2;
    /* rr is in range 0.0..1.0*sqrt(2) */
    return rr;
}

static inline float
calculate_r_sample (float x, float y, float aspect_ratio, float sn, float cs) {

    const float yyr=(y*cs-x*sn)*aspect_ratio;
    const float xxr=y*sn+x*cs;
    const float r = (yyr*yyr + xxr*xxr);
    return r;
}

static inline float
sign_point_in_line (float px, float py, float vx, float vy) {

    return (px - vx) * (-vy) - (vx) * (py - vy);
}

static inline void
closest_point_to_line (float  lx, float  ly, float  px, float  py, float *ox, float *oy) {

    const float l2 = lx*lx + ly*ly;
    const float ltp_dot = px*lx + py*ly;
    const float t = ltp_dot / l2;
    *ox = lx * t;
    *oy = ly * t;
}


/* This works by taking the visibility at the nearest point
 * and dividing by 1.0 + delta.
 *
 * - nearest point: point where the dab has more influence
 * - farthest point: point at a fixed distance away from
 *                   the nearest point
 * - delta: how much occluded is the farthest point relative
 *          to the nearest point
 */
inline float KisMyPaintSurface::calculate_rr_antialiased (int  xp, int  yp, float x, float y,
                          float aspect_ratio, float sn, float cs, float one_over_radius2,
                          float r_aa_start) {

    /* calculate pixel position and borders in a way
     * that the dab's center is always at zero */
    float pixel_right = x - (float)xp;
    float pixel_bottom = y - (float)yp;
    float pixel_center_x = pixel_right - 0.5f;
    float pixel_center_y = pixel_bottom - 0.5f;
    float pixel_left = pixel_right - 1.0f;
    float pixel_top = pixel_bottom - 1.0f;

    float nearest_x, nearest_y; /* nearest to origin, but still inside pixel */
    float farthest_x, farthest_y; /* farthest from origin, but still inside pixel */
    float r_near, r_far, rr_near, rr_far;
    float center_sign, rad_area_1, visibilityNear, delta, delta2;

    /* Dab's center is inside pixel? */
    if( pixel_left<0 && pixel_right>0 &&
        pixel_top<0 && pixel_bottom>0 )
    {
        nearest_x = 0;
        nearest_y = 0;
        r_near = rr_near = 0;
    }
    else
    {
        closest_point_to_line( cs, sn, pixel_center_x, pixel_center_y, &nearest_x, &nearest_y );
        nearest_x = CLAMP( nearest_x, pixel_left, pixel_right );
        nearest_y = CLAMP( nearest_y, pixel_top, pixel_bottom );
        /* XXX: precision of "nearest" values could be improved
         * by intersecting the line that goes from nearest_x/Y to 0
         * with the pixel's borders here, however the improvements
         * would probably not justify the perdormance cost.
         */
        r_near = calculate_r_sample( nearest_x, nearest_y, aspect_ratio, sn, cs );
        rr_near = r_near * one_over_radius2;
    }

    /* out of dab's reach? */
    if( rr_near > 1.0f )
        return rr_near;

    /* check on which side of the dab's line is the pixel center */
    center_sign = sign_point_in_line( pixel_center_x, pixel_center_y, cs, -sn );

    /* radius of a circle with area=1
     *   A = pi * r * r
     *   r = sqrt(1/pi)
     */
    rad_area_1 = sqrtf( 1.0f / M_PI );

    /* center is below dab */
    if( center_sign < 0 )
    {
        farthest_x = nearest_x - sn*rad_area_1;
        farthest_y = nearest_y + cs*rad_area_1;
    }
    /* above dab */
    else
    {
        farthest_x = nearest_x + sn*rad_area_1;
        farthest_y = nearest_y - cs*rad_area_1;
    }

    r_far = calculate_r_sample( farthest_x, farthest_y, aspect_ratio, sn, cs );
    rr_far = r_far * one_over_radius2;

    /* check if we can skip heavier AA */
    if( r_far < r_aa_start )
        return (rr_far+rr_near) * 0.5f;

    /* calculate AA approximate */
    visibilityNear = 1.0f - rr_near;
    delta = rr_far - rr_near;
    delta2 = 1.0f + delta;
    visibilityNear /= delta2;

    return 1.0f - visibilityNear;
}
/* -- end mypaint code */

inline float KisMyPaintSurface::calculate_alpha_for_rr (float rr, float hardness, float slope1, float slope2) {

  if (rr > 1.0f)
    return 0.0f;
  else if (rr <= hardness)
    return 1.0f + rr * slope1;
  else
    return rr * slope2 - slope2;
}
