#ifndef MULTILINEARRECONSTRUCTOR_HPP
#define MULTILINEARRECONSTRUCTOR_HPP

#ifndef MKL_BLAS
#define MKL_BLAS MKL_DOMAIN_BLAS
#endif

#define EIGEN_USE_MKL_ALL

#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Geometry>
#include <eigen3/Eigen/LU>

#include "ceres/ceres.h"

#include "basicmesh.h"
#include "common.h"
#include "constraints.h"
#include "costfunctions.h"
#include "multilinearmodel.h"
#include "parameters.h"
#include "utils.hpp"

using namespace Eigen;

struct ModelParameters {
  static const int nFACSDim = 47;
  VectorXd Wid;               // identity weights
  VectorXd Wexp, Wexp_FACS;   // expression weights
  Vector3d R;              // rotation
  Vector3d T;                 // translation
};

template <typename Constraint>
struct ReconstructionParameters {
  int imageWidth, imageHeight;
  vector<Constraint> cons;
};

struct MultilinearModelPrior {
  VectorXd Wid_avg, Wexp_avg;
  VectorXd Wid0, Wexp0;       // identity and expression prior
  MatrixXd Uid, Uexp;
  MatrixXd sigma_Wid, sigma_Wexp;
  MatrixXd inv_sigma_Wid, inv_sigma_Wexp;
  double weight_Wid, weight_Wexp;

  void load(const string &filename_id, const string &filename_exp) {
    cout << "loading prior data ..." << endl;
    const string fnwid = filename_id;
    ifstream fwid(fnwid, ios::in | ios::binary);

    int ndims;
    fwid.read(reinterpret_cast<char*>(&ndims), sizeof(int));
    cout << "identity prior dim = " << ndims << endl;

    Wid_avg.resize(ndims);
    Wid0.resize(ndims);
    sigma_Wid.resize(ndims, ndims);

    fwid.read(reinterpret_cast<char*>(Wid_avg.data()), sizeof(double)*ndims);
    fwid.read(reinterpret_cast<char*>(Wid0.data()), sizeof(double)*ndims);
    fwid.read(reinterpret_cast<char*>(sigma_Wid.data()), sizeof(double)*ndims*ndims);
    inv_sigma_Wid = sigma_Wid.inverse();

    int m, n;
    fwid.read(reinterpret_cast<char*>(&m), sizeof(int));
    fwid.read(reinterpret_cast<char*>(&n), sizeof(int));
    cout << "Uid size: " << m << 'x' << n << endl;
    Uid.resize(m, n);
    fwid.read(reinterpret_cast<char*>(Uid.data()), sizeof(double)*m*n);

    fwid.close();

    message("identity prior loaded.");
    /*
    cout << "Wid_avg = " << Wid_avg << endl;
    cout << "Wid0 = " << Wid0 << endl;
    cout << "sigma_Wid = " << sigma_Wid << endl;
    cout << "Uid = " << Uid << endl;
    */

    message("processing identity prior.");
    inv_sigma_Wid = sigma_Wid.inverse();
    message("done");

    const string fnwexp = filename_exp;
    ifstream fwexp(fnwexp, ios::in | ios::binary);

    fwexp.read(reinterpret_cast<char*>(&ndims), sizeof(int));
    cout << "expression prior dim = " << ndims << endl;

    Wexp0.resize(ndims);
    Wexp_avg.resize(ndims);
    sigma_Wexp.resize(ndims, ndims);

    fwexp.read(reinterpret_cast<char*>(Wexp_avg.data()), sizeof(double)*ndims);
    fwexp.read(reinterpret_cast<char*>(Wexp0.data()), sizeof(double)*ndims);
    fwexp.read(reinterpret_cast<char*>(sigma_Wexp.data()), sizeof(double)*ndims*ndims);
    inv_sigma_Wexp = sigma_Wexp.inverse();

    fwexp.read(reinterpret_cast<char*>(&m), sizeof(int));
    fwexp.read(reinterpret_cast<char*>(&n), sizeof(int));
    cout << "Uexp size: " << m << 'x' << n << endl;
    Uexp.resize(m, n);
    fwexp.read(reinterpret_cast<char*>(Uexp.data()), sizeof(double)*m*n);

    fwexp.close();

    message("expression prior loaded.");
    /*
    cout << "Wexp_avg = " << Wexp_avg << endl;
    cout << "Wexp0 = " << Wexp0 << endl;
    cout << "sigma_Wexp = " << sigma_Wexp << endl;
    cout << "Uexp = " << Uexp << endl;
    */
    message("processing expression prior.");
    inv_sigma_Wexp = sigma_Wexp.inverse();
    message("done.");
  }
};

struct OptimizationParameters {
  int maxIters;
  double errorThreshold;
  double errorDiffThreshold;
};

template <typename Constraint>
class SingleImageReconstructor {
public:
  SingleImageReconstructor(){}
  void LoadModel(const string &filename);
  void LoadPriors(const string &filename_id, const string &filename_exp);
  void SetIndices(const vector<int> &indices_vec) { indices = indices_vec; }
  void SetConstraints(const vector<Constraint> &cons) { params_recon.cons = cons; }
  void SetContourIndices(const vector<vector<int>> &contour_points) { contour_indices = contour_points; }
  void SetImageSize(int w, int h) {
    params_recon.imageWidth = w;
    params_recon.imageHeight = h;
  }
  void SetMesh(const BasicMesh& mesh_in) {
    mesh = mesh_in;
  }
  void SetOptimizationParameters(const OptimizationParameters &params) {
    params_opt = params;
  }

  bool Reconstruct();

  const Vector3d& GetRotation() const { return params_model.R; }
  const Vector3d& GetTranslation() const { return params_model.T; }
  const VectorXd& GetIdentityWeights() const { return params_model.Wid; }
  const VectorXd& GetExpressionWeights() const { return params_model.Wexp_FACS; }
  const Tensor1& GetGeometry() const { return model.GetTM(); }
  const CameraParameters GetCameraParameters() const { return params_cam; }
  const vector<int> GetIndices() const { return indices; }
  vector<int> GetUpdatedIndices() const {
    vector<int> idxs;
    for(int i=0;i<params_recon.cons.size();++i) {
      idxs.push_back(params_recon.cons[i].vidx);
    }
    return idxs;
  }

protected:
  void OptimizeForPose(int max_iterations);
  void OptimizeForExpression(int iteration);
  void OptimizeForIdentity(int iteration);
  void UpdateContourIndices();

private:
  MultilinearModel model, model_projected;
  vector<int> indices;
  vector<vector<int>> contour_indices;
  MultilinearModelPrior prior;
  BasicMesh mesh;   // for mesh topology

  CameraParameters params_cam;
  ModelParameters params_model;
  ReconstructionParameters<Constraint> params_recon;
  OptimizationParameters params_opt;
};

template <typename Constraint>
void SingleImageReconstructor<Constraint>::LoadModel(const string &filename)
{
  model = MultilinearModel(filename);
}

template <typename Constraint>
void SingleImageReconstructor<Constraint>::LoadPriors(const string &filename_id, const string &filename_exp)
{
  prior.load(filename_id, filename_exp);
}

template <typename Constraint>
bool SingleImageReconstructor<Constraint>::Reconstruct()
{
  // Initialize parameters
  cout << "Reconstruction begins." << endl;

  // Camera parameters
  params_cam.focal_length = glm::vec2(1000.0, 1000.0);
  params_cam.image_plane_center = glm::vec2(params_recon.imageWidth * 0.5,
                                            params_recon.imageHeight * 0.5);
  params_cam.image_size = glm::vec2(params_recon.imageWidth,
                                    params_recon.imageHeight);

  // Model parameters

  // Make a neutral face
  params_model.Wexp_FACS.resize(ModelParameters::nFACSDim);
  params_model.Wexp_FACS(0) = 1.0;
  for(int i=1;i<ModelParameters::nFACSDim;++i) params_model.Wexp_FACS(i) = 1e-6;
  params_model.Wexp = params_model.Wexp_FACS.transpose() * prior.Uexp;

  // Use average identity
  params_model.Wid = prior.Wid_avg;

  // No rotation and translation
  params_model.R = Vector3d(0, 0, 0);
  params_model.T = Vector3d(0, 0, -1.0);

  model.ApplyWeights(params_model.Wid, params_model.Wexp);

  for(int i=0;i<indices.size();++i) {
    params_recon.cons[i].vidx = indices[i];
  }

  // Assign lower weights to contour points
  const int num_contour_points = 15;
  for(int i=0;i<num_contour_points;++i) {
    params_recon.cons[i].weight = 0.9;
  }

  // Reconstruction begins
  const int kMaxIterations = 8;
  prior.weight_Wid = 10.0;
  prior.weight_Wexp = 0.1;
  int iters = 0;
  while( iters++ < kMaxIterations ) {
    OptimizeForPose(30);
    OptimizeForExpression(iters);

    OptimizeForPose(30);
    OptimizeForIdentity(iters);

    OptimizeForPose(30);
    model.ApplyWeights(params_model.Wid, params_model.Wexp);
    mesh.UpdateVertices(model.GetTM());
    mesh.ComputeNormals();
    UpdateContourIndices();

    // Adjust weights
    prior.weight_Wid -= 1.0;
    prior.weight_Wexp -= 0.01;
    for(int i=0;i<num_contour_points;++i) {
      params_recon.cons[i].weight = sqrt(params_recon.cons[i].weight);
    }
  }

  cout << "Reconstruction done." << endl;
  model.ApplyWeights(params_model.Wid, params_model.Wexp);

  return true;
}

template <typename Constraint>
void SingleImageReconstructor<Constraint>::OptimizeForPose(int max_iters) {
  ceres::Problem problem;
  vector<double> params{params_model.R[0], params_model.R[1], params_model.R[2],
                        params_model.T[0], params_model.T[1], params_model.T[2]};

  for(int i=0;i<indices.size();++i) {
    auto model_i = model.project(vector<int>(1, indices[i]));
    model_i.ApplyWeights(params_model.Wid, params_model.Wexp);
    ceres::CostFunction *cost_function =
      new ceres::NumericDiffCostFunction<PoseCostFunction, ceres::CENTRAL, 2, 6>(
        new PoseCostFunction(model_i,
                             params_recon.cons[i],
                             params_cam));
    problem.AddResidualBlock(cost_function, NULL, params.data());
  }

  ceres::Solver::Options options;
  options.max_num_iterations = max_iters;
  //options.minimizer_type = ceres::LINE_SEARCH;
  //options.line_search_direction_type = ceres::STEEPEST_DESCENT;
  options.minimizer_progress_to_stdout = true;
  ceres::Solver::Summary summary;
  Solve(options, &problem, &summary);

  cout << summary.BriefReport() << endl;
  Vector3d newR(params[0], params[1], params[2]);
  Vector3d newT(params[3], params[4], params[5]);
  cout << "R: " << params_model.R.transpose() << " -> " << newR.transpose() << endl;
  cout << "T: " << params_model.T.transpose() << " -> " << newT.transpose() << endl;
  params_model.R = newR;
  params_model.T = newT;
}

template <typename Constraint>
void SingleImageReconstructor<Constraint>::OptimizeForExpression(int iteration) {
  // Create view matrix
  auto Rmat = glm::eulerAngleYXZ(params_model.R[0], params_model.R[1], params_model.R[2]);
  glm::dmat4 Tmat = glm::translate(glm::dmat4(1.0),
                                   glm::dvec3(params_model.T[0], params_model.T[1], params_model.T[2]));
  glm::dmat4 Mview = Tmat * Rmat;

  VectorXd params = params_model.Wexp_FACS;

  double puple_distance = glm::distance(0.5 * (params_recon.cons[28].data + params_recon.cons[30].data),
                                        0.5 * (params_recon.cons[32].data + params_recon.cons[34].data));
  double prior_scale = puple_distance / 100.0;

  // Define the optimization problem
  ceres::Problem problem;

  for(int i=0;i<indices.size();++i) {
    auto model_i = model.project(vector<int>(1, indices[i]));
    model_i.ApplyWeights(params_model.Wid, params_model.Wexp);
    ceres::DynamicNumericDiffCostFunction<ExpressionCostFunction> *cost_function =
      new ceres::DynamicNumericDiffCostFunction<ExpressionCostFunction>(
        new ExpressionCostFunction(model_i,
                                   params_recon.cons[i],
                                   params.size(),
                                   Mview,
                                   prior.Uexp,
                                   params_cam));
    cost_function->AddParameterBlock(params.size());
    cost_function->SetNumResiduals(2);
    problem.AddResidualBlock(cost_function, NULL, params.data());
  }

  ceres::DynamicNumericDiffCostFunction<ExpressionPriorCostFunction> *prior_cost_function =
    new ceres::DynamicNumericDiffCostFunction<ExpressionPriorCostFunction>(
      new ExpressionPriorCostFunction(prior.Wexp_avg, prior.inv_sigma_Wexp, prior.Uexp, prior.weight_Wexp * prior_scale));
  prior_cost_function->AddParameterBlock(params.size());
  prior_cost_function->SetNumResiduals(1);
  problem.AddResidualBlock(prior_cost_function, NULL, params.data());

  for(int i=0;i<params.size();++i) {
    problem.SetParameterLowerBound(params.data(), i, -1.0);
    problem.SetParameterUpperBound(params.data(), i, 1.0);
  }

  // Solve it
  ceres::Solver::Options options;
  options.max_num_iterations = iteration * 5;
  //options.minimizer_type = ceres::LINE_SEARCH;
  //options.line_search_direction_type = ceres::STEEPEST_DESCENT;
  options.minimizer_progress_to_stdout = true;
  ceres::Solver::Summary summary;
  Solve(options, &problem, &summary);

  cout << summary.BriefReport() << endl;

  // Update the model parameters
  cout << params_model.Wexp_FACS.transpose() << endl
       << " -> " << endl
       << params.transpose() << endl;
  params_model.Wexp_FACS = params;
  params_model.Wexp = params_model.Wexp_FACS.transpose() * prior.Uexp;
}

template <typename Constraint>
void SingleImageReconstructor<Constraint>::OptimizeForIdentity(int iteration) {
  // Create view matrix
  auto Rmat = glm::eulerAngleYXZ(params_model.R[0], params_model.R[1], params_model.R[2]);
  glm::dmat4 Tmat = glm::translate(glm::dmat4(1.0),
                                   glm::dvec3(params_model.T[0], params_model.T[1], params_model.T[2]));
  glm::dmat4 Mview = Tmat * Rmat;

  VectorXd params = params_model.Wid;

  double puple_distance = glm::distance(0.5 * (params_recon.cons[28].data + params_recon.cons[30].data),
                                        0.5 * (params_recon.cons[32].data + params_recon.cons[34].data));
  double prior_scale = puple_distance / 100.0;

  // Define the optimization problem
  ceres::Problem problem;

  for(int i=0;i<indices.size();++i) {
    auto model_i = model.project(vector<int>(1, indices[i]));
    model_i.ApplyWeights(params_model.Wid, params_model.Wexp);
    ceres::DynamicNumericDiffCostFunction<IdentityCostFunction> *cost_function =
      new ceres::DynamicNumericDiffCostFunction<IdentityCostFunction>(
        new IdentityCostFunction(model_i,
                                 params_recon.cons[i],
                                 params.size(),
                                 Mview,
                                 params_cam));
    cost_function->AddParameterBlock(params.size());
    cost_function->SetNumResiduals(2);
    problem.AddResidualBlock(cost_function, NULL, params.data());
  }

  ceres::DynamicNumericDiffCostFunction<PriorCostFunction> *prior_cost_function =
    new ceres::DynamicNumericDiffCostFunction<PriorCostFunction>(
      new PriorCostFunction(prior.Wid_avg, prior.inv_sigma_Wid, prior.weight_Wid * prior_scale));
  prior_cost_function->AddParameterBlock(params.size());
  prior_cost_function->SetNumResiduals(1);
  problem.AddResidualBlock(prior_cost_function, NULL, params.data());

  // Solve it
  ceres::Solver::Options options;
  options.max_num_iterations = iteration * 5;
  //options.minimizer_type = ceres::LINE_SEARCH;
  //options.line_search_direction_type = ceres::STEEPEST_DESCENT;
  options.minimizer_progress_to_stdout = true;
  ceres::Solver::Summary summary;
  Solve(options, &problem, &summary);

  cout << summary.BriefReport() << endl;

  // Update the model parameters
  cout << params_model.Wid.transpose() << endl
  << " -> " << endl
  << params.transpose() << endl;
  params_model.Wid = params;
}

template <typename Constraint>
void SingleImageReconstructor<Constraint>::UpdateContourIndices() {
  // Create view matrix
  auto Rmat = glm::eulerAngleYXZ(params_model.R[0], params_model.R[1], params_model.R[2]);
  glm::dmat4 Tmat = glm::translate(glm::dmat4(1.0),
                                   glm::dvec3(params_model.T[0], params_model.T[1], params_model.T[2]));
  glm::dmat4 Mview = Tmat * Rmat;

  vector<pair<int, glm::dvec4>> candidates;
  for(int j=0;j<contour_indices.size();++j) {
    vector<double> dot_products(contour_indices[j].size(), 0.0);
    vector<glm::dvec4> contour_vertices(contour_indices[j].size());
    for(int i=0;i<contour_indices[j].size();++i) {
//      auto model_ji = model.project(vector<int>(1, contour_indices[j][i]));
//      model_ji.ApplyWeights(params_model.Wid, params_model.Wexp);
//      auto tm = model_ji.GetTM();
//      glm::dvec4 p0(tm[0], tm[1], tm[2], 1.0);

      Vector3d v_ji = mesh.vertex(contour_indices[j][i]);
      glm::dvec4 p0(v_ji[0], v_ji[1], v_ji[2], 1.0);

      // Apply the rotation and translation as well
      glm::dvec4 p = Mview * p0;
      contour_vertices[i] = p0;

      // Compute the normal for this vertex
      auto n0 = mesh.vertex_normal(contour_indices[j][i]);
      glm::dvec4 n = Rmat * glm::dvec4(n0[0], n0[1], n0[2], 1.0);

      // Compute the dot product of normal and view direction
      dot_products[i] = fabs(glm::dot(glm::dvec3(n.x, n.y, n.z),
                                      glm::dvec3(0, 0, 1.0)));
    }

    auto min_iter = std::min_element(dot_products.begin(), dot_products.end());
    int min_idx = min_iter - dot_products.begin();
    //cout << min_idx << endl;
    candidates.push_back(make_pair(contour_indices[j][min_idx],
                                   contour_vertices[min_idx]));

    if ( min_idx > 0 ) {
      Vector3d v_ji1 = mesh.vertex(contour_indices[j][min_idx-1]);
      glm::dvec4 p1(v_ji1[0], v_ji1[1], v_ji1[2], 1.0);
      candidates.push_back(make_pair(contour_indices[j][min_idx-1],
                                     p1));
    }
    if ( min_idx < contour_indices[j].size()-1 ) {
      Vector3d v_ji1 = mesh.vertex(contour_indices[j][min_idx+1]);
      glm::dvec4 p1(v_ji1[0], v_ji1[1], v_ji1[2], 1.0);
      candidates.push_back(make_pair(contour_indices[j][min_idx+1],
                                     p1));
    }
  }

  // Project all points to image plane and choose the closest ones as new
  // contour points.
  vector<glm::dvec3> projected_points(candidates.size());
  for(int i=0;i<candidates.size();++i) {
    projected_points[i] = ProjectPoint(
      glm::dvec3(candidates[i].second.x,
                 candidates[i].second.y,
                 candidates[i].second.z),
      Mview, params_cam);
    //cout << projected_points[i].x << ", " << projected_points[i].y << endl;
  }

  // Find closest match for each contour point
  const int num_contour_points = 15;
  for(int i=0;i<num_contour_points;++i) {
    vector<double> dists(candidates.size());
    for(int j=0;j<candidates.size();++j) {
      double dx = projected_points[j].x - params_recon.cons[i].data.x;
      double dy = projected_points[j].y - params_recon.cons[i].data.y;
      dists[j] = dx * dx + dy * dy;
    }
    auto min_iter = std::min_element(dists.begin(), dists.end());
    double min_acceptable_dist = 100.0;
    if( sqrt(*min_iter) > min_acceptable_dist ) {
      //cout << sqrt(*min_iter) << endl;
      continue;
    } else {
      //cout << i << ": " << indices[i] << " -> " << candidates[min_iter - dists.begin()].first << endl;
      indices[i] = candidates[min_iter - dists.begin()].first;
      params_recon.cons[i].vidx = candidates[min_iter - dists.begin()].first;
    }
  }
}
#endif // MULTILINEARRECONSTRUCTOR_HPP

