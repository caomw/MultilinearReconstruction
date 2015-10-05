#ifndef COSTFUNCTIONS_H
#define COSTFUNCTIONS_H

#include "common.h"
#include "constraints.h"
#include "multilinearmodel.h"
#include "parameters.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/euler_angles.hpp"
#include <eigen3/Eigen/Dense>
using namespace Eigen;

glm::dvec3 ProjectPoint(const glm::dvec3& p, const glm::dmat4& Mview, const CameraParameters& cam_params) {
  glm::dmat4 Mproj = glm::perspective(45.0,
                                      (double)cam_params.image_size.x / (double)cam_params.image_size.y,
                                      1.0, 10.0);
  glm::ivec4 viewport(0, 0, cam_params.image_size.x, cam_params.image_size.y);
  return glm::project(p, Mview, Mproj, viewport);
}

struct PoseCostFunction {
  PoseCostFunction(const MultilinearModel &model,
                   const Constraint2D &constraint,
                   const CameraParameters &cam_params)
    : model(model), constraint(constraint), cam_params(cam_params) {}

  bool operator()(const double* const params, double* residual) const {
    auto tm = model.GetTM();
    glm::dvec3 p(tm[0], tm[1], tm[2]);

    /*
    glm::dmat4 Mproj = glm::perspective(45.0,
                                        (double)cam_params.image_size.x / (double)cam_params.image_size.y,
                                        1.0, 10.0);
    glm::ivec4 viewport(0, 0, cam_params.image_size.x, cam_params.image_size.y);
    */

    auto Rmat = glm::eulerAngleYXZ(params[0], params[1], params[2]);
    glm::dmat4 Tmat = glm::translate(glm::dmat4(1.0),
                                     glm::dvec3(params[3], params[4], params[5]));
    glm::dmat4 Mview = Tmat * Rmat;

    /// @todo Create projection matrix using camera focal length
    glm::dvec3 q = ProjectPoint(p, Mview, cam_params);

    residual[0] = (q.x - constraint.data.x) * constraint.weight;
    residual[1] = (q.y - constraint.data.y) * constraint.weight;

    /*
    cout << "(" << q.x << ", " << q.y << ")"
         << " vs "
         << "(" << constraint.data.x << ", " << constraint.data.y << ")" << endl;
    */

    return true;
  }

  MultilinearModel model;
  Constraint2D constraint;
  CameraParameters cam_params;
};

struct IdentityCostFunction {
  IdentityCostFunction(const MultilinearModel& model,
                       const Constraint2D& constraint,
                       int params_length,
                       const glm::mat4& Mview,
                       const CameraParameters& cam_params)
    : model(model), constraint(constraint),
      params_length(params_length),
      Mview(Mview), cam_params(cam_params) {}

  bool operator()(const double* const* wid, double* residual) const {
    // Apply the weight vector to the model
    model.UpdateTMWithTM1(Map<const VectorXd>(wid[0], params_length));

    // Project the point to image plane
    auto tm = model.GetTM();
    glm::dvec3 q = ProjectPoint(glm::dvec3(tm[0], tm[1], tm[2]),
                                 Mview,
                                 cam_params);
    // Compute residual
    residual[0] = (q.x - constraint.data.x) * constraint.weight;
    residual[1] = (q.y - constraint.data.y) * constraint.weight;

    return true;
  }

  mutable MultilinearModel model;
  int params_length;

  Constraint2D constraint;
  glm::dmat4 Mview;
  CameraParameters cam_params;
};

struct ExpressionCostFunction {
  ExpressionCostFunction(const MultilinearModel& model,
                         const Constraint2D& constraint,
                         int params_length,
                         const glm::dmat4& Mview,
                         const MatrixXd& Uexp,
                         const CameraParameters& cam_params)
    : model(model), constraint(constraint), params_length(params_length),
      Mview(Mview), Uexp(Uexp), cam_params(cam_params) {}

  bool operator()(const double* const* wexp, double* residual) const {
    VectorXd weights = Map<const VectorXd>(wexp[0], params_length).transpose() * Uexp;

    // Apply the weight vector to the model
    model.UpdateTMWithTM0(weights);

    // Project the point to image plane
    auto tm = model.GetTM();
    glm::dvec3 p(tm[0], tm[1], tm[2]);
    glm::dvec3 q = ProjectPoint(p, Mview, cam_params);
    // Compute residual
    residual[0] = (q.x - constraint.data.x) * constraint.weight;
    residual[1] = (q.y - constraint.data.y) * constraint.weight;
    return true;
  }

  mutable MultilinearModel model;
  int params_length;

  Constraint2D constraint;
  glm::dmat4 Mview;
  const MatrixXd& Uexp;
  CameraParameters cam_params;
};

struct PriorCostFunction {
  PriorCostFunction(const VectorXd& prior_vec, const MatrixXd& inv_cov_mat, double weight)
    : prior_vec(prior_vec), inv_cov_mat(inv_cov_mat), weight(weight) {}

  bool operator()(const double* const* w, double* residual) const {
    const int params_length = prior_vec.size();
    VectorXd diff = Map<const VectorXd>(w[0], params_length) - prior_vec;

    // Simply Mahalanobis distance between w and prior_vec
    residual[0] = sqrt(weight * diff.transpose() * (inv_cov_mat * diff));
    return true;
  }

  const VectorXd& prior_vec;
  const MatrixXd& inv_cov_mat;
  double weight;
};

#endif // COSTFUNCTIONS_H
