#include "interpolation.h"
#include "global_structs.h"
#include "matrices.h"
#include "interpolation_func.h"
#include <cmath>

extern float g_x_target;
extern float g_z_target;
extern float g_interp_x_val;
extern float g_interp_z_val;
extern float g_query_aoit;
extern float g_query_aoil;
extern InterpolInputs g_InterpolInputs;

void interpolation_f(){

	float query_points[2];
	float z_aux;
	g_InterpolInputs.matrix_X = matrix_X;
	g_InterpolInputs.matrix_Z = matrix_Z;

	query_points[0] = (float)fabs(g_InterpolInputs.AOIt);
	query_points[1] = (float)fabs(g_InterpolInputs.AOIl);

	for (int i = 0; i < 2; i++){
		if (query_points[i] < 0) query_points[i] = 0;
		if (query_points[i] > N - 1) query_points[i] = N - 1;
	}

	/*
	 * What: expose the real interpolation inputs used after abs() and clamping.
	 * How: stores AOIt as query index 0 and AOIl as query index 1.
	 * Why: the web UI also shows raw AOI, so these values prove what interpolate() actually receives.
	 */
	g_query_aoit = query_points[0];
	g_query_aoil = query_points[1];

	static float x_coords[N], y_coords[N];
	for (int k = 0; k < N; k++) {
	x_coords[k] = k;
	y_coords[k] = k;
	}

	const float* coords[2] = {x_coords, y_coords}; 
	int n[2] = {N, N};

	z_aux = (interpolate(coords, n, 	g_InterpolInputs.matrix_Z, query_points));
	/*
	 * What: publish the newly calculated movement target without changing the accepted position.
	 * How: interpolation stores its raw output and also updates g_x_target/g_z_target.
	 * Why: calculated targets must not be confused with the theoretical position where the panel actually is.
	 */
	g_interp_x_val = fabs(interpolate(coords, n, 	g_InterpolInputs.matrix_X, query_points));
	g_interp_z_val = fabs(z_aux);
	g_x_target = g_interp_x_val;
	g_z_target = g_interp_z_val;
	
}
