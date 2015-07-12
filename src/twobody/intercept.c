#include <twobody/conic.h>
#include <twobody/anomaly.h>
#include <twobody/true_anomaly.h>
#include <twobody/orbit.h>
#include <twobody/intercept.h>
#include <twobody/math_utils.h>

static int intersect_ranges(
    const double *fs1, const double *fs2,
    int closed,
    double *fs) {
    // calculate two angle ranges fs[0]..fs[1] and fs[2]..fs[3]
    // where ranges fs1 (two pairs) and fs2 (two pairs) overlap (-2pi..2pi)
    // fs[0] = -2pi..pi, fs[1..3] = -pi..pi
    // (only first range may overlap apoapsis)
    for(int i = 0; i < 2; ++i) {
        for(int j = 0; j < 2; ++j) {
            double f0 = fs1[(fs1[2] < fs1[3] ? 2*i : 0) + j];
            double f1 = fs2[(fs2[2] < fs2[3] ? 2*i : 0) + j];
            fs[i*2+j] = j ? fmin(f0, f1) : fmax(f0, f1);
        }
    }

    if(closed &&
        (fs[0] <= -M_PI || zero(fs[0]+M_PI)) &&
        (fs[3] >= M_PI || zero(fs[3]-M_PI))) {
        // ranges overlap at apoapsis -> union ranges -2pi
        fs[0] = fmin(fs[0], fs[2]-2.0*M_PI);
        fs[1] = fmax(fs[1], fs[3]-2.0*M_PI);
        fs[2] = 1.0; fs[3] = -1.0;
    }

    if((fs[1] >= fs[2] || zero(fs[1]-fs[2])) &&
        fs[2] < fs[3]) {
        // ranges overlap at periapsis -> union ranges
        fs[1] = fs[3];
        fs[2] = 1.0; fs[3] = -1.0;
    }

    if(fs[2] < fs[3] && fs[3] > M_PI) {
        // one or two ranges, second overlaps apoapsis -> swap ranges -2pi
        double f0 = fs[0], f1 = fs[1];
        fs[0] = fs[2]-2.0*M_PI;
        fs[1] = fs[3]-2.0*M_PI;
        fs[2] = f0; fs[3] = f1;
    }

    if(fs[2] < fs[3] && !(fs[0] < fs[1])) {
        // first range is empty, second is not -> swap ranges
        fs[0] = fs[2]; fs[1] = fs[3];
        fs[2] = 1.0; fs[3] = -1.0;
    }

    if(fs[1] - fs[0] >= 2.0*M_PI) {
        // first range covers full orbit -> -pi..pi
        // (second range must be empty)
        fs[0] = -M_PI; fs[1] = M_PI;
    }

    return (fs[0] < fs[1]) + (fs[2] < fs[3]);
}

int intercept_intersect(
    const struct orbit *orbit1,
    const struct orbit *orbit2,
    double threshold,
    double *fs) {
    // find 0, 1 or 2 ranges of true anomaly (fs[0]..fs[1]) and (fs[2]..fs[3])
    // where orbit1 is between the periapsis and apoapsis of orbit2
    // and closer than threshold to the orbital plane of orbit2

    // radial orbits not handled
    if(orbit_radial(orbit1) || orbit_radial(orbit2))
        return 0;

    // find 0, 1, or 2 ranges of true anomaly (w.r.t. orbit1)
    // where orbit1 and orbit2 may be than threshold

    double p1 = orbit_semi_latus_rectum(orbit1);
    double p2 = orbit_semi_latus_rectum(orbit2);
    double e1 = orbit_eccentricity(orbit1);
    double e2 = orbit_eccentricity(orbit2);

    // apoapsis-periapsis test
    double ap1 = conic_apoapsis(p1, e1), pe1 = conic_periapsis(p1, e1);
    double ap2 = conic_apoapsis(p2, e2), pe2 = conic_periapsis(p2, e2);
    if( (conic_closed(e1) && ap1 <= pe2 - threshold) ||
        (conic_closed(e2) && ap2 <= pe1 - threshold))
        return 0;

    // true anomaly between target apoapsis/periapsis +/- threshold
    double maxf = conic_max_true_anomaly(e1);
    double fpe = conic_circular(e1) ? 0.0 :
        true_anomaly_from_radius(p1, e1, pe2 - threshold);
    double fap = (conic_circular(e1) || !conic_closed(e2)) ? maxf :
        true_anomaly_from_radius(p1, e1, ap2 + threshold);

    double f1 = fmin(fap, fpe), f2 = fmax(fap, fpe);

    double fs1[4] = { 1.0, -1.0, 1.0, -1.0 };
    if(conic_closed(e1) && zero(f1) && !(f2 < M_PI)) {
        // intersects anywhere on orbit (f = -pi .. pi)
        fs1[0] = -2.0*M_PI; fs1[1] = 2.0*M_PI;
    } else if(zero(f1)) {
        // intersect near periapsis (f = -f2 .. f2)
        fs1[0] = -f2; fs1[1] = f2;
    } else if(conic_closed(e1) && !(f2 < M_PI)) {
        // intersect near apoapsis (f < -f1, f > f1)
        fs1[0] = -2.0*M_PI; fs1[1] = -f1;
        fs1[2] = f1; fs1[3] = 2.0*M_PI;
    } else {
        // two intersects (-f2 < f < -f1, f1 < f < f2)
        fs1[0] = -f2; fs1[1] = -f1;
        fs1[2] = f1; fs1[3] = f2;
    }

    // ascending node
    vec4d nodes = cross(orbit1->normal_axis, orbit2->normal_axis);
    double N2 = dot(nodes, nodes), N = sqrt(N2);
    int coplanar = N2 < DBL_EPSILON;

    double fs2[4] = { -M_PI, M_PI, 1.0, -1.0 };
    if(!coplanar) {
        // relative inclination
        double reli = sign(dot(orbit1->normal_axis, orbit2->normal_axis)) *
            asin(clamp(-1.0, 1.0, N));

        // true anomaly of ascending/descending node
        double f_an = sign(dot(orbit1->minor_axis, nodes)) *
            acos(clamp(-1.0, 1.0, dot(orbit1->major_axis, nodes)/N));
        double f_dn = f_an - sign(f_an) * M_PI;

        for(int i = 0; i < 2; ++i) {
            double f_node = i == 0 ? fmin(f_an, f_dn) : fmax(f_an, f_dn);

            // distance at node
            double r = p1 / (1.0 + e1*cos(f_node));

            // spherical trigonometry sine law
            double delta_f = asin(
                clamp(-1.0, 1.0,
                    sin(threshold / (2.0*r)) / sin(fabs(reli) / 2.0)));

            fs2[i*2+0] = f_node - delta_f;
            fs2[i*2+1] = f_node + delta_f;
        }
    }

    return intersect_ranges(fs1, fs2, conic_closed(e1), fs);
}

int intercept_times(
    const struct orbit *orbit1,
    const struct orbit *orbit2,
    double t0, double t1,
    const double *fs,
    double *intercept_times,
    int max_times) {
    // find at most max_times time ranges between (t0..t1) where
    // orbit1 is between (fs[0]..fs[1]) or (fs[2]..fs[3]) and
    // orbit2 is between (fs[4]..fs[5]) or (fs[6]..fs[7])

    double mu = orbit_gravity_parameter(orbit1);
    const struct orbit *orbits[2] = { orbit1, orbit2 };

    // find time ranges corresponding to true anomaly ranges
    double times[2][4] = { { 1.0, -1.0, 1.0, -1.0 }, { 1.0, -1.0, 1.0, -1.0 } };
    double periods[2] = { 0.0, 0.0 }; // orbital period
    int n_orbit[2] = { 0, 0 }; // number of complete orbits to t0
    for(int o = 0; o < 2; ++o) {
        double p = orbit_semi_latus_rectum(orbits[o]);
        double e = orbit_eccentricity(orbits[o]);
        double t_pe = orbit_periapsis_time(orbits[o]);
        double n = conic_mean_motion(mu, p, e);

        double f_t0 = -M_PI, f_t1 = M_PI;
        if(!conic_closed(e)) {
            // restrict true anomaly to range within time (t0..t1)
            double M_t0 = (t0 - t_pe) * n, M_t1 = (t1 - t_pe) * n;
            f_t0 = anomaly_mean_to_true(e, M_t0);
            f_t1 = anomaly_mean_to_true(e, M_t1);
        }

        for(int i = 0; i < 2; ++i) {
            double f0 = fs[4*o+2*i+0], f1 = fs[4*o+2*i+1];

            if(f0 >= f1) // empty true anomaly range
                continue;

            for(int j = 0; j < 2; ++j) {
                // calculate time for true anomaly
                double f = clamp(f_t0, f_t1, fs[4*o+2*i+j]);
                double M = anomaly_true_to_mean(e, f) -
                    (f < -M_PI ? 2.0*M_PI : 0.0);

                times[o][2*i+j] = t_pe + M/n;
            }
        }

        if(conic_closed(e)) {
            double P = 2.0*M_PI / n;
            periods[o] = P;
            n_orbit[o] = (int)trunc((t0 - t_pe)/P + (t_pe > t0 ? -0.5 : 0.5));
        }
    }

    int isect[2] = { 0, 0 };
    int num_times = 0;
    double t = t0;
    while(t < t1 && num_times < max_times) {
        double trange[2][2];

        // time interval on this orbital period
        for(int o = 0; o < 2; ++o) {
            double period = n_orbit[o] * periods[o];
            trange[o][0] = times[o][2*isect[o]+0] + period;
            trange[o][1] = times[o][2*isect[o]+1] + period;
        }

        // overlapping time interval
        double t_begin = fmax(t, fmax(trange[0][0], trange[1][0]));
        double t_end = fmin(t1, fmin(trange[0][1], trange[1][1]));
        t = fmax(t0, t_end);

        // non-empty interval found
        if(t_begin < t_end) {
            if(num_times >= 1 &&
                (t_begin <= intercept_times[2*num_times-1] ||
                zero(t_begin - intercept_times[2*num_times-1]))) {
                // merge to previous time interval
                intercept_times[2*num_times-1] = t_end;
            } else {
                // add new time interval
                intercept_times[2*num_times+0] = t_begin;
                intercept_times[2*num_times+1] = t_end;
                num_times += 1;
            }
        }

        // advance to next intersect range
        int advance = trange[0][1] < trange[1][1] ? 0 : 1;
        isect[advance] += 1;

        double fnext0 = fs[4*advance+2*isect[advance]+0];
        double fnext1 = fs[4*advance+2*isect[advance]+1];
        if(isect[advance] == 2 || fnext0 >= fnext1) {
            // advance to next orbit
            if(!orbit_elliptic(orbits[advance]))
                break; // open orbit, search exhausted

            isect[advance] = 0;
            n_orbit[advance] += 1;
        }
    }

    return num_times;
}

#include <stdio.h> // XXX: kill me ! 

int intercept_dump(
    const struct orbit *orbit1,
    const struct orbit *orbit2,
    double t0, double t1) {

    double mu = orbit_gravity_parameter(orbit1);

    double p[2] = {
        orbit_semi_latus_rectum(orbit1),
        orbit_semi_latus_rectum(orbit2)
    };
    double e[2] = {
        orbit_eccentricity(orbit1),
        orbit_eccentricity(orbit2)
    };
    double n[2] = {
        conic_mean_motion(mu, p[0], e[0]),
        conic_mean_motion(mu, p[1], e[1])
    };
    double t_pe[2] = {
        orbit_periapsis_time(orbit1),
        orbit_periapsis_time(orbit2),
    };

    FILE *file = fopen("intercept_distance.txt", "w");

    int max_steps = 100;
    for(int i = 0; i < max_steps; ++i) {
        double t = t0 + (i / (double)(max_steps-1)) * (t1-t0);

        vec4d pos[2], vel[2];
        for(int o = 0; o < 2; ++o) {
            const struct orbit *orbit = o ? orbit2 : orbit1;
            double M = (t - t_pe[o]) * n[o];
            double E = anomaly_mean_to_eccentric(e[o], M);
            pos[o] = orbit_position_eccentric(orbit, E);
            vel[o] = orbit_velocity_eccentric(orbit, E);
        }

        double dist = mag(pos[1]-pos[0]);
        double vrel = dot(vel[1]-vel[0], pos[1]-pos[0])/dist;

        fprintf(file, "%lf\t%lf\t%lf\n", t, dist, vrel);
    }

    fclose(file);
    return 0;
}

double intercept_search(
    const struct orbit *orbit1,
    const struct orbit *orbit2,
    double t0, double t1,
    double threshold,
    double target_distance,
    int max_steps,
    struct intercept *intercept) {

    //max_steps = 50; // XXX: !!!
    //intercept_dump(orbit1, orbit2, t0, t1);

    double mu = orbit_gravity_parameter(orbit1);

    const struct orbit *orbits[2] = { orbit1, orbit2 };
    double p[2] = {
        orbit_semi_latus_rectum(orbit1),
        orbit_semi_latus_rectum(orbit2)
    };
    double e[2] = {
        orbit_eccentricity(orbit1),
        orbit_eccentricity(orbit2)
    };
    double n[2] = {
        conic_mean_motion(mu, p[0], e[0]),
        conic_mean_motion(mu, p[1], e[1])
    };
    double t_pe[2] = {
        orbit_periapsis_time(orbit1),
        orbit_periapsis_time(orbit2),
    };

    double vmax =
        conic_periapsis_velocity(mu, p[0], e[0]) +
        conic_periapsis_velocity(mu, p[1], e[1]);
    double amax =
        mu/conic_periapsis(p[0], e[0]) +
        mu/conic_periapsis(p[1], e[1]);

    //vec4d nodes = cross(orbit1->normal_axis, orbit2->normal_axis);
    //int coplanar = dot(nodes, nodes) < DBL_EPSILON;

    vec4d pos[2], vel[2];
    vec4d dr, dv;
    double dist = NAN, vrel = NAN;
    double E[2] = { // eccentric anomaly, initialize to mean anomaly at t0
        (t0 - t_pe[0]) * n[0],
        (t0 - t_pe[1]) * n[1],
    };

    double min_dt = (t1-t0) / (max_steps/2);

    //FILE *file = fopen("intercept_steps.txt", "w");

    double t = t0, prev_time = NAN, t_end = t1;
    int prev_sgn = 0;
    for(int step = 0; step < max_steps; ++step) {
        for(int o = 0; o < 2; ++o) {
            double M = (t - t_pe[o]) * n[o];
// XXX: enable this
#if 0
            if(conic_parabolic(e[o]))
                E[o] = anomaly_mean_to_eccentric(e[o], M);
            else
                E[o] = anomaly_eccentric_iterate(e[o], M, E[o], -1);
#endif
            E[o] = anomaly_mean_to_eccentric(e[o], M);

            pos[o] = orbit_position_eccentric(orbits[o], E[o]);
            vel[o] = orbit_velocity_eccentric(orbits[o], E[o]);
        }

        dr = pos[1] - pos[0];
        dv = vel[1] - vel[0];
        dist = mag(dr);
        vrel = dot(dr, dv) / dist;
        int sgn = sign(vrel) * sign(dist - target_distance);

        //fprintf(file, "%d\t%lf\t%lf\t%lf\n", step, t, dist, vrel);

        double dt = min_dt;

        //printf("[%03d] t: %3.3lf\tdist: %3.3lf\tvrel: %3.3lf\n", step, t, dist, vrel);

        if(zero(square(dist - fmax(0.0, target_distance))/square(threshold))) {
            // minimization finished
            //printf("[%03d] minimization finished\n", step);
            // XXX: t_end?!
            break;
        } else if(sgn < 0 && dist < threshold && t_end > t0) {
            // below threshold, do minimization step
            //printf("[%03d] minimization step\n", step);
            dt = (target_distance - dist) / vrel;
            break;
        } else if(sgn > 0 && prev_sgn < 0 &&
            (t-prev_time)*vmax + threshold > fabs(dist - target_distance) &&
            (t-prev_time) > (t1-t_end)/(max_steps - step)) {
            // closest approach found, move time backwards and adjust time step
            //printf("[%03d] sign change, t: %lf\tprev_time: %lf\tmin_dt: %lf\n", step, t, prev_time, min_dt);

            t_end = fmax(t, t_end);
            min_dt = (t - prev_time) / 2;
            dt = min_dt;

            t = prev_time;
            sgn = prev_sgn;
        } else if(t > t1) {
            // search exhausted
            //printf("[%03d] search exhausted t: %lf\n", step, t);
            break;
        } else {
            // searching, skip ahead in time
            double deltas[] = {
                -1.0, //XXX:
                // distance at maximum velocity
                fabs((dist - target_distance)-threshold) / vmax,
                // distance at relative velocity + max acceleration (may be NaN)
                // 1/2 amax * t^2 + vrel t + (distance-target_distance) = 0
                //(vrel + sqrt(vrel*vrel - 4.0*amax*(dist-target_distance))) / amax,
            };

            for(unsigned i = 0; i < sizeof(deltas)/sizeof(double); ++i)
                if(isfinite(deltas[i]))
                    dt = fmax(dt, deltas[i]);

            //printf("[%03d] skip ahead %2.2lfx, t: %3.3lf\n", step, dt/min_dt, t);

        }

        //if(zero(dt)) // TODO: terminate when dt gets small enough
            //break;

        t_end = fmax(t_end, t);
        prev_time = t; prev_sgn = sgn;
        t += dt;
// XXX: enable this
#if 0
        for(int o = 0; o < 2; ++o)
            if(!conic_parabolic(e[o]))
                E[o] += anomaly_dEdM(e[o], E[o]) * n[o] * dt;
#endif
    }
    //
    // if(both circular && coplanar && prograde) t_end = t1;

    intercept->position[0] = pos[0]; intercept->position[1] = pos[1];
    intercept->velocity[0] = vel[0]; intercept->velocity[1] = vel[1];
    intercept->relative_position = dr; intercept->relative_velocity = dv;

    intercept->mu = mu;
    intercept->time = t;
    intercept->distance = dist;
    intercept->speed = vrel; // vrel or vel[1]-vel[0]

    intercept->E1 = E[0]; intercept->E2 = E[1];
    intercept->xxx1 = NAN; intercept->xxx2 = NAN;

    //fclose(file);

    return t_end; // XXX: return value?
}

int intercept_orbit(
    const struct orbit *orbit1,
    const struct orbit *orbit2,
    double t0, double t1,
    double threshold, double target_distance,
    struct intercept *intercepts,
    int max_intercepts,
    int max_steps) {

    double fs[8];
    for(int o = 0; o < 2; ++o) {
        if(intercept_intersect(orbit1, orbit2, threshold, fs + o*4) == 0)
            return t1;
    }

    int max_times = 4*max_intercepts;
    double times[max_times];
    int ts = intercept_times(orbit1, orbit2, t0, t1, fs, times, max_times);

    int num_intercepts = 0;
    for(int time = 0; time < ts; ++time) {
        double t_begin = times[2*time+0], t_end = times[2*time+1];

        double t = t_begin;
        while(t < t_end && num_intercepts < max_intercepts) {
            struct intercept *intercept = intercepts + num_intercepts;
            t = intercept_search(
                orbit1, orbit2,
                t, t_end,
                threshold, target_distance,
                max_steps, intercept);
            if(intercept->distance <= threshold)
                num_intercepts += 1;

            // XXX: run intercept_search in loop until return value >= t_end
            break;
        }
    }

    return num_intercepts;
}
