/*
  Copyright 2015 IRIS AS

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef OPM_PARALLELDEBUGOUTPUT_HEADER_INCLUDED
#define OPM_PARALLELDEBUGOUTPUT_HEADER_INCLUDED

#include <opm/core/grid.h>
#include <opm/core/simulator/SimulatorState.hpp>
#include <opm/core/simulator/WellState.hpp>
#include <opm/core/wells/WellsManager.hpp>

#include <opm/autodiff/WellStateFullyImplicitBlackoil.hpp>
#include <opm/autodiff/WellStateFullyImplicitBlackoil.hpp>

#if HAVE_DUNE_CORNERPOINT
#include <dune/grid/common/p2pcommunicator.hh>
#endif

namespace Opm
{

    class ParallelDebugOutputInterface
    {
    protected:
        ParallelDebugOutputInterface () {}
    public:
        virtual ~ParallelDebugOutputInterface() {}

        // gather solution to rank 0 for EclipseWriter
        virtual bool collectToIORank( const SimulatorState& localReservoirState,
                                      const WellState& localWellState,
                                      const int reportStep ) = 0;

        virtual const SimulatorState& globalReservoirState() const = 0 ;
        virtual const WellState& globalWellState() const = 0 ;
        virtual bool isIORank() const = 0;
        virtual bool isParallel() const = 0;
        virtual int numCells() const = 0 ;
        virtual const int* globalCell() const = 0;
    };

    template <class GridImpl>
    class ParallelDebugOutput : public ParallelDebugOutputInterface
    {
    protected:
        const GridImpl& grid_;

        const SimulatorState* globalState_;
        const WellState*      wellState_;

    public:
        ParallelDebugOutput ( const GridImpl& grid,
                              Opm::EclipseStateConstPtr /* eclipseState */,
                              const int,
                              const double* )
            : grid_( grid ) {}

        // gather solution to rank 0 for EclipseWriter
        virtual bool collectToIORank( const SimulatorState& localReservoirState,
                                      const WellState& localWellState,
                                      const int /* reportStep */)
        {
            globalState_ = &localReservoirState;
            wellState_   = &localWellState;
            return true ;
        }

        virtual const SimulatorState& globalReservoirState() const { return *globalState_; }
        virtual const WellState& globalWellState() const { return *wellState_; }
        virtual bool isIORank () const { return true; }
        virtual bool isParallel () const { return false; }
        virtual int numCells() const { return grid_.number_of_cells; }
        virtual const int* globalCell() const { return grid_.global_cell; }
    };

#if HAVE_DUNE_CORNERPOINT
    template <>
    class ParallelDebugOutput< Dune::CpGrid> : public ParallelDebugOutputInterface
    {
    public:
        typedef Dune::CpGrid Grid;
        typedef typename Grid :: CollectiveCommunication  CollectiveCommunication;

        // global id
        class GlobalCellIndex
        {
            int globalId_;
            int localIndex_;
            bool isInterior_;
        public:
            GlobalCellIndex() : globalId_(-1), localIndex_(-1), isInterior_(true) {}
            void setGhost() { isInterior_ = false; }

            void setId( const int globalId ) { globalId_ = globalId; }
            void setIndex( const int localIndex ) { localIndex_ = localIndex; }

            int localIndex () const { return localIndex_; }
            int id () const { return globalId_; }
            bool isInterior() const { return isInterior_; }
        };

        typedef typename Dune::PersistentContainer< Grid, GlobalCellIndex > GlobalIndexContainer;

        static const int dimension = Grid :: dimension ;

        typedef typename Grid :: LeafGridView             GridView;
        typedef GridView                                  AllGridView;

        typedef Dune :: Point2PointCommunicator< Dune :: SimpleMessageBuffer > P2PCommunicatorType;
        typedef typename P2PCommunicatorType :: MessageBufferType MessageBufferType;

        typedef std::vector< GlobalCellIndex > LocalIndexMapType;

        typedef std::vector<int> IndexMapType;
        typedef std::vector< IndexMapType > IndexMapStorageType;

        class DistributeIndexMapping : public P2PCommunicatorType::DataHandleInterface
        {
        protected:
            const std::vector<int>& distributedGlobalIndex_;
            IndexMapType& localIndexMap_;
            IndexMapStorageType& indexMaps_;
            std::map< const int, const int > globalPosition_;
#ifndef NDEBUG
            std::set< int > checkPosition_;
#endif

        public:
            DistributeIndexMapping( const std::vector<int>& globalIndex,
                                    const std::vector<int>& distributedGlobalIndex,
                                    IndexMapType& localIndexMap,
                                    IndexMapStorageType& indexMaps )
            : distributedGlobalIndex_( distributedGlobalIndex ),
              localIndexMap_( localIndexMap ),
              indexMaps_( indexMaps ),
              globalPosition_()
            {
                const size_t size = globalIndex.size();
                // create mapping globalIndex --> localIndex
                for ( size_t index = 0; index < size; ++index )
                {
                    globalPosition_.insert( std::make_pair( globalIndex[ index ], index ) );
                }

                // on I/O rank we need to create a mapping from local to global
                if( ! indexMaps_.empty() )
                {
                    // for the ioRank create a localIndex to index in global state map
                    IndexMapType& indexMap = indexMaps_.back();
                    const size_t localSize = localIndexMap_.size();
                    indexMap.resize( localSize );
                    for( size_t i=0; i<localSize; ++i )
                    {
                        const int id = distributedGlobalIndex_[ localIndexMap_[ i ] ];
                        indexMap[ i ] = globalPosition_[ id ] ;
#ifndef NDEBUG
                        assert( checkPosition_.find( id ) == checkPosition_.end() );
                        checkPosition_.insert( id );
#endif
                    }
                }
            }

            void pack( const int link, MessageBufferType& buffer )
            {
                // we should only get one link
                if( link != 0 ) {
                    OPM_THROW(std::logic_error,"link in method pack is not 0 as execpted");
                }

                // pack all interior global cell id's
                const int size = localIndexMap_.size();
                buffer.write( size );

                for( int index = 0; index < size; ++index )
                {
                    const int globalIdx = distributedGlobalIndex_[ localIndexMap_[ index ] ];
                    buffer.write( globalIdx );
                }
            }

            void unpack( const int link, MessageBufferType& buffer )
            {
                // get index map for current link
                IndexMapType& indexMap = indexMaps_[ link ];
                assert( ! globalPosition_.empty() );

                // unpack all interior global cell id's
                int numCells = 0;
                buffer.read( numCells );
                indexMap.resize( numCells );
                for( int index = 0; index < numCells; ++index )
                {
                    int globalId = -1;
                    buffer.read( globalId );
                    assert( globalPosition_.find( globalId ) != globalPosition_.end() );
                    indexMap[ index ] = globalPosition_[ globalId ];
#ifndef NDEBUG
                    assert( checkPosition_.find( globalId ) == checkPosition_.end() );
                    checkPosition_.insert( globalId );
#endif
                }
            }
        };

        enum { ioRank = 0 };

        ParallelDebugOutput( const Dune::CpGrid& otherGrid,
                             Opm::EclipseStateConstPtr eclipseState,
                             const int numPhases,
                             const double* permeability )
            : grid_(),
              eclipseState_( eclipseState ),
              permeability_( permeability ),
              toIORankComm_( otherGrid.comm() ),
              isIORank_( otherGrid.comm().rank() == ioRank )
        {
            const CollectiveCommunication& comm = otherGrid.comm();
            if( comm.size() > 1 )
            {
                std::set< int > send, recv;
                // the I/O rank receives from all other ranks
                if( isIORank() )
                {
                    // copy grid
                    grid_.reset( new Dune::CpGrid(otherGrid ) );
                    grid_->switchToGlobalView();
                    Dune::CpGrid& globalGrid = *grid_;

                    // initialize global state with correct sizes
                    globalReservoirState_.init( globalGrid.numCells(), globalGrid.numFaces(), numPhases );

                    // copy global cartesian index
                    globalIndex_ = globalGrid.globalCell();

                    unsigned int count = 0;
                    auto gridView = globalGrid.leafGridView();
                    for( auto it = gridView.begin< 0 >(),
                         end = gridView.end< 0 >(); it != end; ++it, ++count )
                    {
                    }
                    assert( count == globalIndex_.size() );

                    for(int i=0; i<comm.size(); ++i)
                    {
                        if( i != ioRank )
                        {
                            recv.insert( i );
                        }
                    }
                }
                else // all other simply send to the I/O rank
                {
                    send.insert( ioRank );
                }

                localIndexMap_.clear();
                localIndexMap_.reserve( otherGrid.size( 0 ) );

                unsigned int index = 0;
                auto localView = otherGrid.leafGridView();
                for( auto it = localView.begin< 0 >(),
                     end = localView.end< 0 >(); it != end; ++it, ++index )
                {
                    const auto element = *it ;
                    // only store interior element for collection
                    if( element.partitionType() == Dune :: InteriorEntity )
                    {
                        localIndexMap_.push_back( index );
                    }
                }

                // insert send and recv linkage to communicator
                toIORankComm_.insertRequest( send, recv );

                if( isIORank() )
                {
                    // need an index map for each rank
                    indexMaps_.clear();
                    indexMaps_.resize( comm.size() );
                }

                // distribute global id's to io rank for later association of dof's
                DistributeIndexMapping distIndexMapping( globalIndex_, otherGrid.globalCell(), localIndexMap_, indexMaps_ );
                toIORankComm_.exchange( distIndexMapping );
            }
            else // serial run
            {
                // copy global cartesian index
                globalIndex_ = otherGrid.globalCell();
            }
        }

        class PackUnPackSimulatorState : public P2PCommunicatorType::DataHandleInterface
        {
            const SimulatorState& localState_;
            SimulatorState& globalState_;
            const WellState& localWellState_;
            WellState& globalWellState_;
            const IndexMapType& localIndexMap_;
            const IndexMapStorageType& indexMaps_;

        public:
            PackUnPackSimulatorState( const SimulatorState& localState,
                                      SimulatorState& globalState,
                                      const WellState& localWellState,
                                      WellState& globalWellState,
                                      const IndexMapType& localIndexMap,
                                      const IndexMapStorageType& indexMaps,
                                      const bool isIORank )
            : localState_( localState ),
              globalState_( globalState ),
              localWellState_( localWellState ),
              globalWellState_( globalWellState ),
              localIndexMap_( localIndexMap ),
              indexMaps_( indexMaps )
            {
                if( isIORank )
                {
                    // add missing data to global state
                    for( size_t i=globalState_.cellData().size();
                         i<localState.cellData().size(); ++i )
                    {
                        const size_t components = localState.cellData()[ i ].size() / localState.numCells();
                        assert( components * localState.numCells() == localState.cellData()[ i ].size() );
                        globalState_.registerCellData( localState.cellDataNames()[ i ], components );
                    }

                    MessageBufferType buffer;
                    pack( 0, buffer );
                    // the last index map is the local one
                    doUnpack( indexMaps.back(), buffer );
                }
            }

            // pack all data associated with link
            void pack( const int link, MessageBufferType& buffer )
            {
                // we should only get one link
                if( link != 0 ) {
                    OPM_THROW(std::logic_error,"link in method pack is not 0 as execpted");
                }

                // write all cell data registered in local state
                const size_t numCells = localState_.numCells();
                const size_t numCellData = localState_.cellData().size();
                for( size_t d=0; d<numCellData; ++d )
                {
                    const std::vector< double >& data = localState_.cellData()[ d ];
                    const size_t stride = data.size() / numCells ;
                    assert( numCells * stride == data.size() );

                    for( size_t i=0; i<stride; ++i )
                    {
                        // write all data from local state to buffer
                        write( buffer, localIndexMap_, data, i, stride );
                    }
                }

                // write all data from local well state to buffer
                writeWells( buffer );
            }

            void doUnpack( const IndexMapType& indexMap, MessageBufferType& buffer )
            {
                // read all cell data registered in local state
                const size_t numCells = globalState_.numCells();
                const size_t numCellData = globalState_.cellData().size();
                for( size_t d=0; d<numCellData; ++d )
                {
                    std::vector< double >& data = globalState_.cellData()[ d ];
                    const size_t stride = data.size() / numCells ;
                    assert( numCells * stride == data.size() );

                    for( size_t i=0; i<stride; ++i )
                    {
                        // write all data from local state to buffer
                        read( buffer, indexMap, data, i, stride );
                    }
                }

                // read well data from buffer
                readWells( buffer );
            }

            // unpack all data associated with link
            void unpack( const int link, MessageBufferType& buffer )
            {
                doUnpack( indexMaps_[ link ], buffer );
            }

        protected:
            template <class Vector>
            void write( MessageBufferType& buffer, const IndexMapType& localIndexMap,
                        const Vector& vector,
                        const unsigned int offset = 0, const unsigned int stride = 1 ) const
            {
                unsigned int size = localIndexMap.size();
                buffer.write( size );
                assert( vector.size() >= stride * size );
                for( unsigned int i=0; i<size; ++i )
                {
                    const unsigned int index = localIndexMap[ i ] * stride + offset;
                    assert( index < vector.size() );
                    buffer.write( vector[ index ] );
                }
            }

            template <class Vector>
            void read( MessageBufferType& buffer,
                       const IndexMapType& indexMap,
                       Vector& vector,
                       const unsigned int offset = 0, const unsigned int stride = 1 ) const
            {
                unsigned int size = 0;
                buffer.read( size );
                assert( size == indexMap.size() );
                for( unsigned int i=0; i<size; ++i )
                {
                    const unsigned int index = indexMap[ i ] * stride + offset;
                    assert( index < vector.size() );
                    buffer.read( vector[ index ] );
                }
            }

            void writeString( MessageBufferType& buffer, const std::string& s) const
            {
                const int size = s.size();
                buffer.write( size );
                for( int i=0; i<size; ++i )
                {
                    buffer.write( s[ i ] );
                }
            }

            void readString( MessageBufferType& buffer, std::string& s) const
            {
                int size = -1;
                buffer.read( size );
                s.resize( size );
                for( int i=0; i<size; ++i )
                {
                    buffer.read( s[ i ] );
                }
            }

            void writeWells( MessageBufferType& buffer ) const
            {
                int nWells = localWellState_.wellMap().size();
                buffer.write( nWells );
                auto end = localWellState_.wellMap().end();
                for( auto it = localWellState_.wellMap().begin(); it != end; ++it )
                {
                    const std::string& name = it->first;
                    const int wellIdx = it->second[ 0 ];

                    // write well name
                    writeString( buffer, name );

                    // write well data
                    buffer.write( localWellState_.bhp()[ wellIdx ] );
                    buffer.write( localWellState_.thp()[ wellIdx ] );
                    const int wellRateIdx = wellIdx * localWellState_.numPhases();
                    for( int np=0; np<localWellState_.numPhases(); ++np )
                        buffer.write( localWellState_.wellRates()[ wellRateIdx + np ] );

                    // TODO: perfRates and perfPress, need to figure out the index
                    // mapping there.
                }
            }

            void readWells( MessageBufferType& buffer )
            {
                int nWells = -1;
                buffer.read( nWells );
                // unpack all wells that have been sent
                std::string name ;
                for( int well = 0; well < nWells ; ++well )
                {
                    // read well name for local identification
                    readString( buffer, name );

                    // unpack values
                    auto it = globalWellState_.wellMap().find( name );
                    if( it == globalWellState_.wellMap().end() )
                    {
                        OPM_THROW(std::logic_error,"global state does not contain well " <<  name );
                    }
                    const int wellIdx = it->second[ 0 ];

                    buffer.read( globalWellState_.bhp()[ wellIdx ] );
                    buffer.read( globalWellState_.thp()[ wellIdx ] );
                    const int wellRateIdx = wellIdx * globalWellState_.numPhases();
                    for( int np=0; np<globalWellState_.numPhases(); ++np )
                        buffer.read( globalWellState_.wellRates()[ wellRateIdx + np ] );

                    // TODO: perfRates and perfPress, need to figure out the index
                    // mapping there.
                }
            }
        };

        // gather solution to rank 0 for EclipseWriter
        bool collectToIORank( const SimulatorState& localReservoirState,
                              const WellState& localWellState,
                              const int reportStep )
        {
            if( isIORank() )
            {
                Dune::CpGrid& globalGrid = *grid_;
                // Create wells and well state.
                WellsManager wells_manager(eclipseState_,
                                           reportStep,
                                           Opm::UgGridHelpers::numCells( globalGrid ),
                                           Opm::UgGridHelpers::globalCell( globalGrid ),
                                           Opm::UgGridHelpers::cartDims( globalGrid ),
                                           Opm::UgGridHelpers::dimensions( globalGrid ),
                                           Opm::UgGridHelpers::cell2Faces( globalGrid ),
                                           Opm::UgGridHelpers::beginFaceCentroids( globalGrid ),
                                           permeability_,
                                           false);

                const Wells* wells = wells_manager.c_wells();
                globalWellState_.init(wells, globalReservoirState_, globalWellState_ );
            }

            PackUnPackSimulatorState packUnpack( localReservoirState, globalReservoirState_,
                                                 localWellState, globalWellState_,
                                                 localIndexMap_, indexMaps_, isIORank() );

            //toIORankComm_.exchangeCached( packUnpack );
            toIORankComm_.exchange( packUnpack );
#ifndef NDEBUG
            // mkae sure every process is on the same page
            toIORankComm_.barrier();
#endif
            return isIORank();
        }

        const SimulatorState& globalReservoirState() const { return globalReservoirState_; }
        const WellState& globalWellState() const { return globalWellState_; }

        bool isIORank() const
        {
            return isIORank_;
        }

        bool isParallel() const
        {
            return toIORankComm_.size() > 1;
        }

        int numCells () const { return globalIndex_.size(); }
        const int* globalCell () const
        {
            assert( ! globalIndex_.empty() );
            return globalIndex_.data();
        }

    protected:
        std::unique_ptr< Dune::CpGrid > grid_;
        Opm::EclipseStateConstPtr       eclipseState_;
        const double*                   permeability_;
        P2PCommunicatorType             toIORankComm_;
        IndexMapType                    globalIndex_;
        IndexMapType                    localIndexMap_;
        IndexMapStorageType             indexMaps_;
        SimulatorState                  globalReservoirState_;
        // this needs to be revised
        WellStateFullyImplicitBlackoil  globalWellState_;
        // true if we are on I/O rank
        const bool                      isIORank_;
    };
#endif // #if HAVE_DUNE_CORNERPOINT

} // end namespace Opm
#endif