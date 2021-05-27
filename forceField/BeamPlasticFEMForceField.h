/******************************************************************************
*                 SOFA, Simulation Open-Framework Architecture                *
*                    (c) 2006 INRIA, USTL, UJF, CNRS, MGH                     *
*                                                                             *
* This program is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This program is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this program. If not, see <http://www.gnu.org/licenses/>.        *
*******************************************************************************
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#pragma once
#include <BeamPlastic/config.h>
#include <BeamPlastic/constitutiveLaw/PlasticConstitutiveLaw.h>
#include <BeamPlastic/quadrature/gaussian.h>

#include <sofa/core/behavior/ForceField.h>
#include <SofaBaseTopology/TopologyData.h>

#include <SofaEigen2Solver/EigenSparseMatrix.h>
#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/Geometry>
#include <string>


namespace sofa::plugin::beamplastic::component::forcefield
{

namespace _beamplasticfemforcefield_
{

using sofa::component::topology::TopologyDataHandler;
using sofa::component::topology::EdgeData;
using sofa::plugin::beamplastic::component::constitutivelaw::PlasticConstitutiveLaw;

/** \class BeamPlasticFEMForceField
 *  \brief Compute Finite Element forces based on 6D plastic beam elements.
 * 
 *  This class extends the BeamFEMForceField component to nonlinear plastic
 *  behaviours. The main difference with the linear elastic scenario is that
 *  the stiffness matrix used in the force computations is no longer constant
 *  and has to be recomputed at each time step (as soon as plastic deformation
 *  occurs).
 *  This type of mechanical behaviour allows to simulate irreversible
 *  deformation, which typically occurs in metals.
 */
template<class DataTypes>
class SOFA_BeamPlastic_API BeamPlasticFEMForceField : public core::behavior::ForceField<DataTypes>
{

public:

    SOFA_CLASS(SOFA_TEMPLATE(BeamPlasticFEMForceField,DataTypes), SOFA_TEMPLATE(core::behavior::ForceField,DataTypes));

    typedef typename DataTypes::Real        Real        ;
    typedef typename DataTypes::Coord       Coord       ;
    typedef typename DataTypes::Deriv       Deriv       ;
    typedef typename DataTypes::VecCoord    VecCoord    ;
    typedef typename DataTypes::VecDeriv    VecDeriv    ;
    typedef typename DataTypes::VecReal     VecReal     ;
    typedef Data<VecCoord>                  DataVecCoord;
    typedef Data<VecDeriv>                  DataVecDeriv;
    typedef VecCoord Vector;

    typedef unsigned int Index;
    typedef core::topology::BaseMeshTopology::Edge Element;
    typedef sofa::helper::vector<core::topology::BaseMeshTopology::Edge> VecElement;
    typedef helper::vector<unsigned int> VecIndex;
    typedef defaulttype::Vec<3, Real> Vec3;
    typedef sofa::helper::types::RGBAColor RGBAColor;

    /** \enum class MechanicalState
     *  \brief Types of mechanical state associated with the (Gauss) integration
     *  points. The POSTPLASTIC state corresponds to points which underwent plastic
     *  deformation, but on which constraints were released so that the plasticity
     *  process stopped.
     */
    enum class MechanicalState {
        ELASTIC = 0,
        PLASTIC = 1,
        POSTPLASTIC = 2,
    };

protected:
    /// Vector representing the displacement of a beam element.
    typedef defaulttype::Vec<12, Real> Displacement;
    /// Matrix for rigid transformations like rotations.
    typedef defaulttype::Mat<3, 3, Real> Transformation;
    /// Stiffness matrix associated to a beam element.
    typedef defaulttype::Mat<12, 12, Real> StiffnessMatrix;

    /**
     * \struct BeamInfo
     * \brief Data structure containing the main characteristics of the beam
     * elements. This includes mechanical and geometric parameters (Young's
     * modulus, Poisson ratio, length, section dimensions, ...), computation
     * variables (stiffness matrix, plasticity history, ...) and visualisation
     * data (shape functions, discretisation parameters).
     */
    struct BeamInfo
    {
        /*********************************************************************/
        /*                     Virtual Displacement method                   */
        /*********************************************************************/

        /// Precomputed stiffness matrix, used for elastic deformation.
        StiffnessMatrix _Ke_loc;
        /**
         * Linearised stiffness matrix (tangent stiffness), updated at each time
         * step for plastic deformation.
         */
        StiffnessMatrix _Kt_loc;

        /// Homogeneous type to a 4th order tensor, in Voigt notation.
        typedef Eigen::Matrix<double, 6, 6> BehaviourMatrix;
        /**
         * Generalised Hooke's law (4th order tensor connecting strain and stress,
         * expressed in Voigt notation)
         */
        BehaviourMatrix _materialBehaviour;

        /**
         * \brief Integration ranges for Gaussian reduced integration.
         * Data structure defined in the quadrature library used here for
         * Gaussian integration, containing the limits of the integration
         * ranges. Here the integration is performed in 3D, and the variable
         * contains 6 real numbers, corresponding to 3 pairs of limits. These
         * numbers depend on the beam element dimensions.
         */
        ozp::quadrature::detail::Interval<3> _integrationInterval;

        /// Matrix form of the beam element shape functions
        typedef Eigen::Matrix<double, 3, 12> shapeFunction;
        /// Shape function matrices, evaluated in each Gauss point used in reduced integration.
        helper::fixed_array<shapeFunction, 27> _N;
        // TO DO : define the "27" constant properly ! static const ? ifdef global definition ?

        /// Homogeneous to the derivative of shapeFunction type
        typedef Eigen::Matrix<double, 6, 12> deformationGradientFunction;
        /// Derivatives of the shape function matrices in _N, also evaluated in each Gauss point
        helper::fixed_array<deformationGradientFunction, 27> _BeMatrices;

        /// Mechanical states (elastic, plastic, or postplastic) of all gauss points in the beam element.
        helper::fixed_array<MechanicalState, 27> _pointMechanicalState;
        /**
         * Indicates which type of mechanical computation should be used.
         * The meaning of the three cases is the following :
         *   - ELASTIC: all the element Gauss points are in an ELASTIC state
         *   - PLASTIC: at least one Gauss point is in a PLASTIC state.
         *   - POSTPLASTIC: Gauss points are either in an ELASTIC or POSTPLASTIC state.
         */
        MechanicalState _beamMechanicalState;

        //---------- Plastic variables ----------//

        /// History of plastic strain, one tensor for each Gauss point in the element.
        helper::fixed_array<Eigen::Matrix<double, 6, 1>, 27> _plasticStrainHistory;
        /**
         * Effective plastic strain, for each Gauss point in the element.
         * The effective plastic strain is only used to compute the tangent
         * modulus if it is not constant.
         */
        helper::fixed_array<Real, 27> _effectivePlasticStrains;

        /// Tensor representing the yield surface centre, one for each Gauss point in the element.
        helper::fixed_array<Eigen::Matrix<double, 6, 1>, 27> _backStresses;
        /// Yield threshold, one for each Gauss point in the element.
        helper::fixed_array<Real, 27> _localYieldStresses;

        //---------- Visualisation ----------//

        /// Number of interpolation segments to visualise the centreline of the beam element
        int _nbCentrelineSeg = 10;

        /// Precomputation of the shape functions matrices for each centreline point coordinates.
        helper::fixed_array<shapeFunction, 9> _drawN; //TO DO: allow parameterisation of the number of segments
                                                      //       which discretise the centreline (here : 10)
                                                      // NB: we use 9 shape functions because extremity points are known

        /*********************************************************************/

        double _E; ///< Young Modulus
        double _nu; ///< Poisson ratio
        double _L; ///< Length of the beam element
        double _zDim; ///< for rectangular beams: dimension of the cross-section along the local z axis
        double _yDim; ///< for rectangular beams: dimension of the cross-section along the local y axis
        double _G; ///< Shear modulus
        double _Iy; ///< 2nd moment of area with regard to the y axis, for a rectangular beam section
        double _Iz; ///< 2nd moment of area with regard to the z axis, for a rectangular beam section
        double _J; ///< Polar moment of inertia (J = Iy + Iz)
        double _A; ///< Cross-sectional area
        StiffnessMatrix _k_loc; ///< Precomputed stiffness matrix, used only for elastic deformation if d_usePrecomputedStiffness = true

        defaulttype::Quat quat; // TO DO : supress ? Apparently it is not used effectively in the computation, only updated

        /// Initialisation of BeamInfo members from constructor parameters
        void init(double E, double yS, double L, double nu, double zSection, double ySection, bool isTimoshenko);

        /// Output stream
        inline friend std::ostream& operator<< ( std::ostream& os, const BeamInfo& bi )
        {
            os << bi._E << " "
                << bi._nu << " "
                << bi._L << " "
                << bi._zDim << " "
                << bi._yDim << " "
                << bi._G << " "
                << bi._Iy << " "
                << bi._Iz << " "
                << bi._J << " "
                << bi._A << " "
                << bi._Ke_loc << " "
                << bi._Kt_loc << " "
                << bi._k_loc;
            return os;
        }

        /// Input stream
        inline friend std::istream& operator>> ( std::istream& in, BeamInfo& bi )
        {
            in	>> bi._E
                >> bi._nu
                >> bi._L
                >> bi._zDim
                >> bi._yDim
                >> bi._G
                >> bi._Iy
                >> bi._Iz
                >> bi._J
                >> bi._A
                >> bi._Ke_loc
                >> bi._Kt_loc
                >> bi._k_loc;
            return in;
        }
    };

    EdgeData< sofa::helper::vector<BeamInfo> > m_beamsData;

    class BeamFFEdgeHandler : public TopologyDataHandler<core::topology::BaseMeshTopology::Edge,sofa::helper::vector<BeamInfo> >
    {
    public:
        typedef typename BeamPlasticFEMForceField<DataTypes>::BeamInfo BeamInfo;
        BeamFFEdgeHandler(BeamPlasticFEMForceField<DataTypes>* ff, EdgeData<sofa::helper::vector<BeamInfo> >* data)
            :TopologyDataHandler<core::topology::BaseMeshTopology::Edge,sofa::helper::vector<BeamInfo> >(data),ff(ff) {}

        void applyCreateFunction(unsigned int edgeIndex, BeamInfo&,
                                 const core::topology::BaseMeshTopology::Edge& e,
                                 const sofa::helper::vector<unsigned int> &,
                                 const sofa::helper::vector< double > &);

    protected:
        BeamPlasticFEMForceField<DataTypes>* ff;

    };

    virtual void reset() override;

    /**************************************************************************/
    /*                     Virtual Displacement Method                        */
    /**************************************************************************/

public:

    typedef defaulttype::Vec<12, Real> nodalForces; ///<  Intensities of the nodal forces in the Timoshenko beam element
    // TO DO : is this type really useful ?

    typedef Eigen::Matrix<double, 6, 1> VoigtTensor2; ///< Symmetrical tensor of order 2, written with Voigt notation
    typedef Eigen::Matrix<double, 9, 1> VectTensor2; ///< Symmetrical tensor of order 2, written with vector notation
    typedef Eigen::Matrix<double, 6, 6> VoigtTensor4; ///< Symmetrical tensor of order 4, written with Voigt notation
    typedef Eigen::Matrix<double, 9, 9> VectTensor4; ///< Symmetrical tensor of order 4, written with vector notation
    typedef Eigen::Matrix<double, 12, 1> EigenDisplacement; ///< Nodal displacement

protected:

    // Rather than computing the elastic stiffness matrix _Ke_loc by Gaussian
    // reduced integration, we can use a precomputed form, as the matrix remains
    // constant during deformation. The precomputed form _k_loc can be found in
    // litterature, for instance in : Theory of Matrix Structural Analysis,
    // Przemieniecki, 1968, McGraw-Hill, New-York.
    // /!\ This option does not imply that all computations will be made with
    // linear elasticity using _k_loc. It only means that _k_loc will be used
    // instead of _Ke_loc, saving the time of one Gaussian integration per beam
    // element. For purely elastic beam elements, the BeamFEMForceField component
    // should be used.
    Data<bool> d_usePrecomputedStiffness;

    /**
     * In the elasto-plastic model, the tangent operator can be computed either
     * in a straightforward way, or in a way consistent with the radial return
     * algorithm. This data field is used to determine which method will be used.
     * For more information on the consistent tangent operator, we recommend
     * reading the following publications :
     *  - Consistent tangent operators for rate-independent elastoplasticity, Simo and Taylor, 1985
     *  - Studies in anisotropic plasticity with reference to the Hill criterion, De Borst and Feenstra, 1990
     */
    Data<bool> d_useConsistentTangentOperator;

    /**
     * Computes the elastic stiffness matrix _Ke_loc using reduced intergation.
     * The alternative is a precomputation of the elastic stiffness matrix, which is
     * possible for beam elements. The corresponding matrix _k_loc is close of the
     * reduced integration matrix _Ke_loc.
     */
    void computeVDStiffness(int i, Index a, Index b);
    /// Computes the generalised Hooke's law matrix.
    void computeMaterialBehaviour(int i, Index a, Index b);

     /// Used to store stress tensor information (in Voigt notation) for each of the 27 points of integration.
    typedef helper::fixed_array<VoigtTensor2, 27> gaussPointStresses;
    /// Stress tensors fo each Gauss point in every beam element, computed at the previous time step.
    /// These stresses are required for the iterative radial return algorithm if plasticity is detected.
    helper::vector<gaussPointStresses> m_prevStresses;
    /// Stress tensors corresponding to the elastic prediction step of the radial return algorithm.
    /// These are stored for the update of the tangent stiffness matrix
    helper::vector<gaussPointStresses> m_elasticPredictors;

    /// Position at the last time step, to handle increments for the plasticity resolution
    VecCoord m_lastPos;

    /**
     * Indicates if the plasticity model is perfect plasticity, or if hardening
     * is represented.
     * The only hardening model we implement is a linear combination of isotropic
     * and kinematic hardening, as described in :
     * Theoretical foundation for large scale computations for nonlinear material
     * behaviour, Hugues(et al) 1984.
     */
    Data<bool> d_isPerfectlyPlastic;

    BeamPlasticFEMForceField<DataTypes>* ff;

    //---------- Plastic modulus ----------//
    /**
     * 1D Contitutive law model, which is in charge of computing the
     * plastic modulus during plastic deformation.
     * The constitutive law is used to retrieve a non-constant plastic
     * modulus, with computePlasticModulusFromStress or
     * computePlasticModulusFromStress, but the computeConstPlasticModulus
     * method can be used instead.
     */
    std::unique_ptr<PlasticConstitutiveLaw<DataTypes>> m_ConstitutiveLaw;
    Data<std::string> d_modelName; ///< name of the model, for specialisation

    double computePlasticModulusFromStress(const Eigen::Matrix<double, 6, 1>& stressState);
    double computePlasticModulusFromStrain(int index, int gaussPointId);
    double computeConstPlasticModulus();
    //-------------------------------------//

    /// Tests if the stress tensor of a material point in an elastic state
    /// actually corresponds to plastic deformation.
    bool goToPlastic(const VoigtTensor2 &stressTensor, const double yieldStress, const bool verbose=FALSE);
    /// Tests if the new stress tensor of a material point in a plastic state
    /// actually corresponds to an elastic (incremental) deformation
    bool goToPostPlastic(const VoigtTensor2 &stressTensor, const VoigtTensor2 &stressIncrement,
                         const bool verbose = FALSE);

    /// Computes local displacement of a beam element using the corotational model
    void computeLocalDisplacement(const VecCoord& x, Displacement &localDisp, int i, Index a, Index b);
    /// Computes a displacement increment between to positions of a beam element (with respect to its local frame)
    void computeDisplacementIncrement(const VecCoord& pos, const VecCoord& lastPos, Displacement &currentDisp, Displacement &lastDisp,
                                      Displacement &dispIncrement, int i, Index a, Index b);

    //---------- Force computation ----------//

    /// Force computation and tangent stiffness matrix update for perfect plasticity
    void computeForceWithPerfectPlasticity(Eigen::Matrix<double, 12, 1>& internalForces, const VecCoord& x, int index, Index a, Index b);

    /// Stress increment computation for perfect plasticity, based on the radial return algorithm
    void computePerfectPlasticStressIncrement(int index, int gaussPointIt, const VoigtTensor2& lastStress, VoigtTensor2& newStressPoint,
                                              const VoigtTensor2& strainIncrement, MechanicalState& pointMechanicalState);

    /// Force computation and tangent stiffness matrix update for linear mixed (isotropic and kinematic) hardening
    void computeForceWithHardening(Eigen::Matrix<double, 12, 1>& internalForces, const VecCoord& x, int index, Index a, Index b);

    /// Stress increment computation for linear mixed (isotropic and kinematic) hardening, based on the radial return algorithm
    void computeHardeningStressIncrement(int index, int gaussPointIt, const VoigtTensor2 &lastStress, VoigtTensor2 &newStressPoint,
                                         const VoigtTensor2 &strainIncrement, MechanicalState &pointMechanicalState);

    //---------------------------------------//


    //---------- Auxiliary methods for Voigt to vector notation conversion ----------//

    // TO DO :
    /* The Voigt notation consists in reducing the dimension of symmetrical tensors by
     * not representing explicitly the symmetrical terms. However these termes have to
     * be taken into account in some operations (such as scalar products) for which
     * they have to be represented explicitly.
     * For the moment, we mostly rely on a full vector notation of all symmetrical
     * variables, and convert them afterwards to Voigt notation in order to reduce the
     * storage cost. In the long term, we should implement all generic functions
     * correcting algebric operations made with Voigt variables (such as voigtDotProduct
     * or voigtTensorNorm), and remove the vector notation.
     */

    /// Converts the 6D Voigt representation of a 2nd-order tensor to a 9D vector representation
    VectTensor2 voigtToVect2(const VoigtTensor2 &voigtTensor);
    /// Converts the 6x6 Voigt representation of a 4th-order tensor to a 9x9 matrix representation
    VectTensor4 voigtToVect4(const VoigtTensor4 &voigtTensor);
    /// Converts the 9D vector representation of a 2nd-order tensor to a 6D Voigt representation
    VoigtTensor2 vectToVoigt2(const VectTensor2 &vectTensor);
    /// Converts the 9x9 matrix representation of a 4th-order tensor to a 6x6 Voigt representation
    VoigtTensor4 vectToVoigt4(const VectTensor4 &vectTensor);

    // Special implementation for second-order tensor operations, with the Voigt notation.
    double voigtDotProduct(const VoigtTensor2& t1, const VoigtTensor2& t2);
    double voigtTensorNorm(const VoigtTensor2& t);
    Eigen::Matrix<double, 12, 1> beTTensor2Mult(const Eigen::Matrix<double, 12, 6>& BeT, const VoigtTensor2& T);
    Eigen::Matrix<double, 12, 12> beTCBeMult(const Eigen::Matrix<double, 12, 6>& BeT, const VoigtTensor4& C,
                                             const double nu, const double E);
    //-------------------------------------------------------------------------------//

    /// Computes the deviatoric stress from a tensor in Voigt notation
    VoigtTensor2 deviatoricStress(const VoigtTensor2 &stressTensor);
    /// Computes the equivalent stress from a tensor in Voigt notation
    double equivalentStress(const VoigtTensor2 &stressTensor);
    /// Evaluates the Von Mises yield function for given stress tensor (in Voigt notation) and yield stress
    double vonMisesYield(const VoigtTensor2 &stressTensor, const double yieldStress);
    /// Computes the Von Mises yield function gradient (in Voigt notation) at a given stress tensor (in Voigt notation)
    VoigtTensor2 vonMisesGradient(const VoigtTensor2 &stressTensor);
    /// Computes the Von Mises yield function hessian (in matrix notation) at a given stress tensor (in Voigt notation)
    VectTensor4 vonMisesHessian(const VoigtTensor2 &stressTensor, const double yieldStress);

    //----- Alternative expressions of the above functions with vector notations -----//
    /// Computes the equivalent stress from a tensor in vector notation
    double vectEquivalentStress(const VectTensor2 &stressTensor);
    /// Evaluates the Von Mises yield function for given stress tensor (in vector notation) and yield stress
    double vectVonMisesYield(const VectTensor2 &stressTensor, const double yieldStress);
    /// Computes the Von Mises yield function gradient (in vector notation) at a given stress tensor (in vector notation)
    VectTensor2 vectVonMisesGradient(const VectTensor2 &stressTensor);

    //----- Alternative functions using the deviatoric stress expression -----//
    // TO DO : is deviatoric computation more efficient than direct computation ?
    /// Computes the equivalent stress from a tensor in Voigt notation, using the deviatoric stress
    double devEquivalentStress(const VoigtTensor2& stressTensor);
    /// Evaluates the Von Mises yield function for given stress tensor (in Voigt notation), using the deviatoric stress
    double devVonMisesYield(const VoigtTensor2& stressTensor, const double yieldStress);
    /// Computes the Von Mises yield function gradient (in Voigt notation) at a given stress tensor (in Voigt notation),
    ///  using the deviatoric stress
    VoigtTensor2 devVonMisesGradient(const VoigtTensor2& stressTensor);

    //Methods called by addForce, addDForce and addKToMatrix when deforming plasticly
    void accumulateNonLinearForce(VecDeriv& f, const VecCoord& x, int i, Index a, Index b);
    void applyNonLinearStiffness(VecDeriv& df, const VecDeriv& dx, int i, Index a, Index b, double fact);
    void updateTangentStiffness(int i, Index a, Index b);


    /**********************************************************/


    const VecElement *m_indexedElements;

    Data<Real> d_poissonRatio;
    Data<Real> d_youngModulus;
    Data<Real> d_yieldStress;
    Data<Real> d_zSection;
    Data<Real> d_ySection;
    Data<bool> d_useSymmetricAssembly;
    Data<bool> d_isTimoshenko;

    double m_lastUpdatedStep;

    defaulttype::Quat& beamQuat(int i);

    sofa::core::topology::BaseMeshTopology* m_topology;
    std::unique_ptr<BeamFFEdgeHandler> m_edgeHandler;

    BeamPlasticFEMForceField();
    BeamPlasticFEMForceField(Real poissonRatio, Real youngModulus, Real yieldStress, Real zSection, Real ySection, bool useVD,
                        bool isPlasticMuller, bool isTimoshenko, bool isPlasticKrabbenhoft, bool isPerfectlyPlastic,
                        helper::vector<defaulttype::Quat> localOrientations);
    virtual ~BeamPlasticFEMForceField() = default;

public:

    virtual void init();
    virtual void bwdInit();
    virtual void reinit();
    virtual void reinitBeam(unsigned int i);

    virtual void addForce(const sofa::core::MechanicalParams* /*mparams*/, DataVecDeriv &  dataF, const DataVecCoord &  dataX , const DataVecDeriv & dataV );
    virtual void addDForce(const sofa::core::MechanicalParams* /*mparams*/, DataVecDeriv&   datadF , const DataVecDeriv&   datadX );
    virtual void addKToMatrix(const sofa::core::MechanicalParams* mparams, const sofa::core::behavior::MultiMatrixAccessor* matrix );

    // TO DO : necessary ?
    virtual SReal getPotentialEnergy(const core::MechanicalParams* /*mparams*/, const DataVecCoord&  /* x */) const
    {
        serr << "Get potentialEnergy not implemented" << sendl;
        return 0.0;
    }

    void draw(const core::visual::VisualParams* vparams);
    void computeBBox(const core::ExecParams* params, bool onlyVisible) override;

    void setBeam(unsigned int i, double E, double yS, double L, double nu, double zSection, double ySection);
    void initBeams(size_t size);

protected:

    void drawElement(int i, std::vector< defaulttype::Vector3 > &gaussPoints,
                     std::vector< defaulttype::Vector3 > &centrelinePoints,
                     std::vector<RGBAColor> &colours, const VecCoord& x);

    void computeStiffness(int i, Index a, Index b);

};

#if !defined(SOFA_COMPONENT_FORCEFIELD_BEAMPLASTICFEMFORCEFIELD_CPP)
extern template class SOFA_BeamPlastic_API BeamPlasticFEMForceField<defaulttype::Rigid3Types>;
#endif

} // namespace _beamplasticfemforcefield_

} // namespace sofa::plugin::beamplastic::component::forcefield