----------------------------------------------------------
--
--   Lua - Script to perform the Laplace-Problem
--
--   Author: Andreas Vogel / Martin Rupp
--
----------------------------------------------------------

-- make sure that ug_util is in the right path.
-- currently only the path in which you start your application is valid.
dofile("../scripts/ug_util.lua")

-- constants
dim = 2
gridName = "unit_square_tri_neumann.ugx"
numRefs = 4

-- name function (will be removed in the future, do not use them)
_INNER_ = 0
_BND_ = 1
_NEUMANN_BND_ = 2

--------------------------------
-- User Data Functions (begin)
--------------------------------

function ourDiffTensor(x, y, t)
return	1, 0, 
0, 1
end

function ourVelocityField(x, y, t)
return	0, 0
end

function ourReaction(x, y, t)
return	0
end

function ourRhs(x, y, t)
--local s = 2*math.pi
--return	s*s*(math.sin(s*x) + math.sin(s*y))
return -2*y
end

function ourNeumannBnd(x, y, t)
--local s = 2*math.pi
--return -s*math.cos(s*x)
return true, -2*x*y
end

function ourDirichletBnd(x, y, t)
--local s = 2*math.pi
--return true, math.sin(s*x) + math.sin(s*y)
return true, x*x*y
end

--------------------------------
-- User Data Functions (end)
--------------------------------

-- create Instance of a Domain
print("Create Domain.")
dom = Domain()

-- load domain
print("Load Domain from File.")
if LoadDomain(dom, gridName) == false then
print("Loading Domain failed.")
exit()
end

-- get subset handler
sh = dom:subset_handler()
if sh:num_subsets() ~= 3 then 
print("Domain must have 3 Subsets for this problem.")
exit()
end
sh:set_subset_name("Inner", 0)
sh:set_subset_name("DirichletBoundary", 1)
sh:set_subset_name("NeumannBoundary", 2)

-- create Refiner
print("Create Refiner")
refiner = GlobalMultiGridRefiner()
refiner:assign_grid(dom:grid())
for i=1,numRefs do
refiner:refine()
end

-- write grid to file for test purpose
SaveDomain(dom, "refined_grid.ugx")

-- create Approximation Space
print("Create ApproximationSpace")
approxSpace = ApproximationSpace(dom)
approxSpace:add_fct("c", "Lagrange", 1)

-------------------------------------------
--  Setup User Functions
-------------------------------------------

-- Diffusion Tensor setup
diffusionMatrix = LuaUserMatrix("ourDiffTensor")
--diffusionMatrix = ConstUserMatrix(1.0)

-- Velocity Field setup
velocityField = LuaUserVector("ourVelocityField")
--velocityField = ConstUserVector(0.0)

-- Reaction setup
reaction = LuaUserNumber("ourReaction")
--reaction = ConstUserNumber(0.0)

-- rhs setup
rhs = LuaUserNumber("ourRhs")
--rhs = ConstUserNumber(0.0)

-- neumann setup
neumann = LuaBoundaryNumber("ourNeumannBnd")
--neumann = ConstUserNumber(0.0)

-- dirichlet setup
dirichlet = LuaBoundaryNumber("ourDirichletBnd")
--dirichlet = ConstBoundaryNumber(0.0)

-----------------------------------------------------------------
--  Setup FV Convection-Diffusion Element Discretization
-----------------------------------------------------------------

elemDisc = ConvectionDiffusion("c", "Inner")
elemDisc:set_disc_scheme("fv1")
elemDisc:set_diffusion_tensor(diffusionMatrix)
elemDisc:set_velocity_field(velocityField)
elemDisc:set_reaction_rate(reaction)
elemDisc:set_source(rhs)

-----------------------------------------------------------------
--  Setup Neumann Boundary
-----------------------------------------------------------------

neumannDisc = FV1NeumannBoundary("Inner")
neumannDisc:add(neumann, "c", "NeumannBoundary")

-----------------------------------------------------------------
--  Setup Dirichlet Boundary
-----------------------------------------------------------------

dirichletBND = DirichletBoundary()
dirichletBND:add(dirichlet, "c", "DirichletBoundary")

-------------------------------------------
--  Setup Domain Discretization
-------------------------------------------

domainDisc = DomainDiscretization(approxSpace)
domainDisc:add(elemDisc)
domainDisc:add(neumannDisc)
domainDisc:add(dirichletBND)

-------------------------------------------
--  Algebra
-------------------------------------------

-- create operator from discretization
linOp = AssembledLinearOperator()
linOp:set_discretization(domainDisc)

-- get grid function
u = GridFunction(approxSpace)
b = GridFunction(approxSpace)

-- set initial value
u:set(1.0)

-- init Operator
linOp:init_op_and_rhs(b)

-- set dirichlet values in start iterate
linOp:set_dirichlet_values(u)

-- write matrix for test purpose
SaveMatrixForConnectionViewer(u, linOp, "Stiffness.mat")
SaveVectorForConnectionViewer(b, "Rhs.vec")

-- create algebraic Preconditioner
jac = JacobiPreconditioner()
jac:set_damp(0.8)
gs = GSPreconditioner()
sgs = SGSPreconditioner()
bgs = BGSPreconditioner()
ilu = ILUPreconditioner()
ilut = ILUTPreconditioner()

-- create GMG
baseConvCheck = StandardConvergenceCheck()
baseConvCheck:set_maximum_steps(500)
baseConvCheck:set_minimum_defect(1e-8)
baseConvCheck:set_reduction(1e-30)
baseConvCheck:set_verbose(false)

-- base = LU()
base = LinearSolver()
base:set_convergence_check(baseConvCheck)
base:set_preconditioner(jac)

gmg = GeometricMultiGrid(approxSpace)
gmg:set_discretization(domainDisc)
gmg:set_surface_level(numRefs)
gmg:set_base_level(0)
gmg:set_base_solver(base)
gmg:set_smoother(jac)
gmg:set_cycle_type(1)
gmg:set_num_presmooth(3)
gmg:set_num_postsmooth(3)

amg = AMGPreconditioner()
amg:set_nu1(2)
amg:set_nu2(2)
amg:set_gamma(1)
amg:set_presmoother(jac)
amg:set_postsmoother(jac)
amg:set_base_solver(base)
amg:set_max_levels(5)
-- amg:set_debug(u)

-- create Convergence Check
convCheck = StandardConvergenceCheck()
convCheck:set_maximum_steps(3)
convCheck:set_minimum_defect(1e-9)
convCheck:set_reduction(1e-9)

-- create Linear Solver
linSolver = LinearSolver()
linSolver:set_preconditioner(amg)
linSolver:set_convergence_check(convCheck)

-- create CG Solver
cgSolver = CGSolver()
cgSolver:set_preconditioner(ilu)
cgSolver:set_convergence_check(convCheck)

-- Apply Solver
ApplyLinearSolver(linOp, u, b, linSolver)

-- Output
WriteGridFunctionToVTK(u, "Solution")