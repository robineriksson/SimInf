/*
 *  SimInf, a framework for stochastic disease spread simulations
 *  Copyright (C) 2015 - 2019  Stefan Widgren
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <Rdefines.h>

#include "SimInf.h"
#include "SimInf_openmp.h"

/**
 * Extract data from a simulated trajectory as a data.frame.
 *
 * @param dm data for the discrete state matrix to transform
 *        to a data.frame.
 * @param dm_i index (1-based) to compartments in 'dm' to include
 *        in the data.frame.
 * @param dm_lbl state names of the data in 'dm'.
 * @param cm data for the continuous state matrix to transform
 *        to a data.frame.
 * @param cm_i index (1-based) to compartments in 'cm' to include
 *        in the data.frame.
 * @param cm_lbl state names of the data in 'cm'.
 * @param tspan a vector of increasing time points for the time
 *        in each column in 'dm' and 'cm'.
 * @param Nn number of nodes in the SimInf_model.
 * @param nodes NULL or an integer vector with (1-based) node
 *        indices of the nodes to include in the data.frame.
 * @return A data.frame.
 */
SEXP SimInf_trajectory(
    SEXP dm,
    SEXP dm_i,
    SEXP dm_lbl,
    SEXP cm,
    SEXP cm_i,
    SEXP cm_lbl,
    SEXP tspan,
    SEXP Nn,
    SEXP nodes)
{
    int *p_int_vec;
    double *p_real_vec;
    R_xlen_t nrow;
    SEXP colnames, result, vec;
    int *p_nodes = Rf_isNull(nodes) ? NULL : INTEGER(nodes);
    R_xlen_t dm_len = XLENGTH(dm_i);
    R_xlen_t dm_stride = Rf_isNull(dm_lbl) ? 0 : XLENGTH(dm_lbl);
    R_xlen_t cm_len = XLENGTH(cm_i);
    R_xlen_t cm_stride = Rf_isNull(cm_lbl) ? 0 : XLENGTH(cm_lbl);
    R_xlen_t ncol = 2 + dm_len + cm_len; /* The '2' is for the 'node' and 'time' columns. */
    R_xlen_t tlen = XLENGTH(tspan);
    R_xlen_t c_Nn = Rf_asInteger(Nn);
    R_xlen_t Nnodes = Rf_isNull(nodes) ? Rf_asInteger(Nn) : XLENGTH(nodes);

    /* Use all available threads in parallel regions. */
    SimInf_set_num_threads(-1);

    /* Create a vector for the column names. */
    PROTECT(colnames = Rf_allocVector(STRSXP, ncol));
    SET_STRING_ELT(colnames, 0, Rf_mkChar("node"));
    SET_STRING_ELT(colnames, 1, Rf_mkChar("time"));
    for (R_xlen_t i = 0; i < dm_len; i++) {
        R_xlen_t j = INTEGER(dm_i)[i] - 1;
        SET_STRING_ELT(colnames, 2 + i, STRING_ELT(dm_lbl, j));
    }
    for (R_xlen_t i = 0; i < cm_len; i++) {
        R_xlen_t j = INTEGER(cm_i)[i] - 1;
        SET_STRING_ELT(colnames, 2 + dm_len + i, STRING_ELT(cm_lbl, j));
    }

    /* Determine the number of rows to hold the trajectory data. */
    nrow = tlen * Nnodes;

    /* Create a list for the 'data.frame'. */
    PROTECT(result = Rf_allocVector(VECSXP, ncol));
    Rf_setAttrib(result, R_NamesSymbol, colnames);

    /* Add the 'data.frame' class attribute to the list. */
    Rf_setAttrib(result, R_ClassSymbol, Rf_mkString("data.frame"));

    /* Add row names to the 'data.frame'. Note that the row names are
     * one-based. */
    PROTECT(vec = Rf_allocVector(INTSXP, nrow));
    p_int_vec = INTEGER(vec);
    #pragma omp parallel for num_threads(SimInf_num_threads())
    for (R_xlen_t i = 0; i < nrow; i++) {
        p_int_vec[i] = i + 1;
    }
    Rf_setAttrib(result, R_RowNamesSymbol, vec);
    UNPROTECT(1);

    /* Add a 'node' identifier column to the 'data.frame'. */
    PROTECT(vec = Rf_allocVector(INTSXP, nrow));
    p_int_vec = INTEGER(vec);
    if (p_nodes != NULL) {
        #pragma omp parallel for num_threads(SimInf_num_threads())
        for (R_xlen_t t = 0; t < tlen; t++) {
            memcpy(&p_int_vec[t * Nnodes], p_nodes, Nnodes * sizeof(int));
        }
    } else {
        #pragma omp parallel for num_threads(SimInf_num_threads())
        for (R_xlen_t t = 0; t < tlen; t++) {
            for (R_xlen_t node = 0; node < Nnodes; node++) {
                p_int_vec[t * Nnodes + node] = node + 1;
            }
        }
    }
    SET_VECTOR_ELT(result, 0, vec);
    UNPROTECT(1);

    /* Add a 'time' column to the 'data.frame'. */
    PROTECT(vec = Rf_allocVector(INTSXP, nrow));
    p_int_vec = INTEGER(vec);
    p_real_vec = REAL(tspan);
    #pragma omp parallel for num_threads(SimInf_num_threads())
    for (R_xlen_t t = 0; t < tlen; t++) {
        for (R_xlen_t node = 0; node < Nnodes; node++) {
            p_int_vec[t * Nnodes + node] = p_real_vec[t];
        }
    }
    SET_VECTOR_ELT(result, 1, vec);
    UNPROTECT(1);

    /* Copy data from the discrete state matrix. */
    for (R_xlen_t i = 0; i < dm_len; i++) {
        int *p_dm = INTEGER(dm) + INTEGER(dm_i)[i] - 1;

        /* Add data for the compartment to the 'data.frame'. */
        PROTECT(vec = Rf_allocVector(INTSXP, nrow));
        p_int_vec = INTEGER(vec);

        if (p_nodes != NULL) {
            /* Note that the node identifiers are one-based. */
            #pragma omp parallel for num_threads(SimInf_num_threads())
            for (R_xlen_t t = 0; t < tlen; t++) {
                for (R_xlen_t node = 0; node < Nnodes; node++) {
                    p_int_vec[t * Nnodes + node] =
                        p_dm[(t * c_Nn + p_nodes[node] - 1) * dm_stride];
                }
            }
        } else {
            #pragma omp parallel for num_threads(SimInf_num_threads())
            for (R_xlen_t t = 0; t < tlen; t++) {
                for (R_xlen_t node = 0; node < Nnodes; node++) {
                    p_int_vec[t * Nnodes + node] =
                        p_dm[(t * c_Nn + node) * dm_stride];
                }
            }
        }

        SET_VECTOR_ELT(result, 2 + i, vec);
        UNPROTECT(1);
    }

    /* Copy data from the continuous state matrix. */
    for (R_xlen_t i = 0; i < cm_len; i++) {
        double *p_cm = REAL(cm) + INTEGER(cm_i)[i] - 1;

        /* Add data for the compartment to the 'data.frame'. */
        PROTECT(vec = Rf_allocVector(REALSXP, nrow));
        p_real_vec = REAL(vec);

        if (p_nodes != NULL) {
            /* Note that the node identifiers are one-based. */
            #pragma omp parallel for num_threads(SimInf_num_threads())
            for (R_xlen_t t = 0; t < tlen; t++) {
                for (R_xlen_t node = 0; node < Nnodes; node++) {
                    p_real_vec[t * Nnodes + node] =
                        p_cm[(t * c_Nn + p_nodes[node] - 1) * cm_stride];
                }
            }
        } else {
            #pragma omp parallel for num_threads(SimInf_num_threads())
            for (R_xlen_t t = 0; t < tlen; t++) {
                for (R_xlen_t node = 0; node < Nnodes; node++) {
                    p_real_vec[t * Nnodes + node] =
                        p_cm[(t * c_Nn + node) * cm_stride];
                }
            }
        }

        SET_VECTOR_ELT(result, 2 + dm_len + i, vec);
        UNPROTECT(1);
    }

    UNPROTECT(2);

    return result;
}
