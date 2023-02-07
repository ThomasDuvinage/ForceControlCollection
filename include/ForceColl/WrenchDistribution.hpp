namespace ForceColl
{
template<class PatchID>
void WrenchDistribution<PatchID>::Configuration::load(const mc_rtc::Configuration & mcRtcConfig)
{
  mcRtcConfig("wrenchWeight", wrenchWeight);
  mcRtcConfig("regularWeight", regularWeight);
  mcRtcConfig("ridgeForceMinMax", ridgeForceMinMax);
}

template<class PatchID>
WrenchDistribution<PatchID>::WrenchDistribution(
    const std::unordered_map<PatchID, std::shared_ptr<Contact>> & contactList,
    const mc_rtc::Configuration & mcRtcConfig)
: contactList_(contactList)
{
  config_.load(mcRtcConfig);

  int colNum = 0;
  for(const auto & contactKV : contactList_)
  {
    colNum += static_cast<int>(contactKV.second->graspMat_.cols());
  }
  resultWrenchRatio_ = Eigen::VectorXd::Zero(colNum);

  QpSolverCollection::QpSolverType qpSolverType = QpSolverCollection::QpSolverType::Any;
  if(mcRtcConfig.has("qpSolverType"))
  {
    qpSolverType = QpSolverCollection::strToQpSolverType(mcRtcConfig("qpSolverType"));
  }
  qpSolver_ = QpSolverCollection::allocateQpSolver(qpSolverType);
}

template<class PatchID>
sva::ForceVecd WrenchDistribution<PatchID>::run(const sva::ForceVecd & desiredTotalWrench,
                                                const Eigen::Vector3d & momentOrigin)
{
  desiredTotalWrench_ = desiredTotalWrench;

  // Return if variable dimension is zero
  if(resultWrenchRatio_.size() == 0)
  {
    resultTotalWrench_ = sva::ForceVecd::Zero();
    return resultTotalWrench_;
  }

  // Construct totalGraspMat
  Eigen::Matrix<double, 6, Eigen::Dynamic> totalGraspMat(6, resultWrenchRatio_.size());
  {
    int colNum = 0;
    for(const auto & contactKV : contactList_)
    {
      totalGraspMat.middleCols(colNum, contactKV.second->graspMat_.cols()) = contactKV.second->graspMat_;
      colNum += static_cast<int>(contactKV.second->graspMat_.cols());
    }
    if(momentOrigin.norm() > 0)
    {
      for(int i = 0; i < colNum; i++)
      {
        // totalGraspMat.col(i).tail<3>() is the force ridge
        totalGraspMat.col(i).head<3>() -= momentOrigin.cross(totalGraspMat.col(i).tail<3>());
      }
    }
  }

  // Solve QP
  {
    int varDim = static_cast<int>(resultWrenchRatio_.size());
    if(qpCoeff_.dim_var_ != varDim)
    {
      qpCoeff_.setup(varDim, 0, 0);
    }
    Eigen::MatrixXd weightMat = config_.wrenchWeight.vector().asDiagonal();
    qpCoeff_.obj_mat_.noalias() = totalGraspMat.transpose() * weightMat * totalGraspMat;
    qpCoeff_.obj_mat_.diagonal().array() += config_.regularWeight;
    qpCoeff_.obj_vec_.noalias() = -1 * totalGraspMat.transpose() * weightMat * desiredTotalWrench_.vector();
    qpCoeff_.x_min_.setConstant(varDim, config_.ridgeForceMinMax.first);
    qpCoeff_.x_max_.setConstant(varDim, config_.ridgeForceMinMax.second);
    resultWrenchRatio_ = qpSolver_->solve(qpCoeff_);
  }

  resultTotalWrench_ = sva::ForceVecd(totalGraspMat * resultWrenchRatio_);

  return resultTotalWrench_;
}

template<class PatchID>
std::unordered_map<PatchID, sva::ForceVecd> WrenchDistribution<PatchID>::calcWrenchList(
    const Eigen::Vector3d & momentOrigin) const
{
  std::unordered_map<PatchID, sva::ForceVecd> wrenchList;
  int wrenchRatioIdx = 0;

  for(const auto & contactKV : contactList_)
  {
    wrenchList.emplace(
        contactKV.first,
        contactKV.second->calcWrench(resultWrenchRatio_.segment(wrenchRatioIdx, contactKV.second->graspMat_.cols()),
                                     momentOrigin));
    wrenchRatioIdx += static_cast<int>(contactKV.second->graspMat_.cols());
  }
  return wrenchList;
}

template<class PatchID>
void WrenchDistribution<PatchID>::addToGUI(mc_rtc::gui::StateBuilder & gui,
                                           const std::vector<std::string> & category,
                                           double forceScale,
                                           double fricPyramidScale)
{
  int wrenchRatioIdx = 0;

  for(const auto & contactKV : contactList_)
  {
    contactKV.second->addToGUI(gui, category, forceScale, fricPyramidScale,
                               resultWrenchRatio_.segment(wrenchRatioIdx, contactKV.second->graspMat_.cols()));
    wrenchRatioIdx += static_cast<int>(contactKV.second->graspMat_.cols());
  }
}
} // namespace ForceColl
