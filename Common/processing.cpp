/*
*	Copyright (C) 2010 Thorsten Liebig (Thorsten.Liebig@gmx.de)
*
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "tools/global.h"
#include "tools/useful.h"
#include "Common/operator_base.h"
#include <algorithm>
#include "processing.h"
#include "time.h"
#include <climits>

Processing::Processing(Engine_Interface_Base* eng_if)
{
	m_Eng_Interface = NULL;
	SetEngineInterface(eng_if);

	Enabled = true;
	m_PS_pos = 0;
	SetPrecision(12);
	ProcessInterval=0;
	m_FD_SampleCount=0;
	m_FD_Interval=0;
	m_weight=1;
	m_Flush = false;
	m_dualMesh = false;
	m_Mesh_Type = CARTESIAN_MESH;
}

Processing::~Processing()
{
	SetEngineInterface(NULL);
	file.close();
}

void Processing::Reset()
{
	m_PS_pos=0;
}

void Processing::SetEngineInterface(Engine_Interface_Base* eng_if)
{
	delete m_Eng_Interface;
	m_Eng_Interface = eng_if;
	if (m_Eng_Interface)
		Op=m_Eng_Interface->GetOperator();
	else
		Op=NULL;
}

bool Processing::CheckTimestep()
{
	if (m_ProcessSteps.size()>m_PS_pos)
	{
		if (m_ProcessSteps.at(m_PS_pos)==m_Eng_Interface->GetNumberOfTimesteps())
		{
			++m_PS_pos;
			return true;
		}
	}
	if (ProcessInterval)
	{
		if (m_Eng_Interface->GetNumberOfTimesteps()%ProcessInterval==0) return true;
	}

	if (m_FD_Interval)
	{
		if (m_Eng_Interface->GetNumberOfTimesteps()%m_FD_Interval==0) return true;
	}
	return false;
}

int Processing::GetNextInterval() const
{
	if (Enabled==false) return -1;
	int next=INT_MAX;
	if (m_ProcessSteps.size()>m_PS_pos)
	{
		next = (int)m_ProcessSteps.at(m_PS_pos)-(int)m_Eng_Interface->GetNumberOfTimesteps();
	}
	if (ProcessInterval!=0)
	{
		int next_Interval = (int)ProcessInterval - (int)m_Eng_Interface->GetNumberOfTimesteps()%ProcessInterval;
		if (next_Interval<next)
			next = next_Interval;
	}

	//check for FD sample interval
	if (m_FD_Interval!=0)
	{
		int next_Interval = (int)m_FD_Interval - (int)m_Eng_Interface->GetNumberOfTimesteps()%m_FD_Interval;
		if (next_Interval<next)
			next = next_Interval;
	}
	return next;
}

void Processing::AddStep(unsigned int step)
{
	if (m_ProcessSteps.size()==0)
		m_ProcessSteps.push_back(step);
	else if (find(m_ProcessSteps.begin(), m_ProcessSteps.end(),step)==m_ProcessSteps.end())
		m_ProcessSteps.push_back(step);
}

void Processing::AddSteps(vector<unsigned int> steps)
{
	for (size_t n=0; n<steps.size(); ++n)
	{
		AddStep(steps.at(n));
	}
}

void Processing::AddFrequency(double freq)
{
	unsigned int nyquistTS = CalcNyquistNum(freq,Op->GetTimestep());

	if (nyquistTS == 0)
	{
		cerr << "Processing::AddFrequency: Requested frequency " << freq << " is too high for the current timestep used... skipping..." << endl;
		return;
	}
	else if (nyquistTS<Op->GetNumberOfNyquistTimesteps())
	{
		cerr << "Processing::AddFrequency: Warning: Requested frequency " << freq << " is higher than maximum excited frequency..." << endl;
	}

	if (m_FD_Interval==0)
		m_FD_Interval = Op->GetNumberOfNyquistTimesteps();
	if (m_FD_Interval>nyquistTS)
		m_FD_Interval = nyquistTS;

	m_FD_Samples.push_back(freq);
}

void Processing::AddFrequency(vector<double> *freqs)
{
	for (size_t n=0; n<freqs->size(); ++n)
	{
		AddFrequency(freqs->at(n));
	}
}

void Processing::DefineStartStopCoord(double* dstart, double* dstop)
{
	if (Op->SnapToMesh(dstart,start,m_dualMesh,m_start_inside)==false)
		cerr << "Processing::DefineStartStopCoord: Warning: Snapped line outside field domain!!" << endl;
	if (Op->SnapToMesh(dstop,stop,m_dualMesh,m_stop_inside)==false)
		cerr << "Processing::DefineStartStopCoord: Warning: Snapped line outside field domain!!" << endl;

	if (g_settings.showProbeDiscretization())
	{
		cerr << m_Name << ": snapped coords: (" << Op->GetDiscLine( 0, start[0], m_dualMesh ) << ","
		     << Op->GetDiscLine( 1, start[1], m_dualMesh ) << "," << Op->GetDiscLine( 2, start[2], m_dualMesh ) << ") -> ("
		     << Op->GetDiscLine( 0, stop[0], m_dualMesh ) << ","<< Op->GetDiscLine( 1, stop[1], m_dualMesh ) << ","
		     << Op->GetDiscLine( 2, stop[2], m_dualMesh ) << ")";
		cerr << "   [" << start[0] << "," << start[1] << "," << start[2] << "] -> ["
		     << stop[0] << "," << stop[1] << "," << stop[2] << "]" << endl;
	}
}

void Processing::OpenFile( string outfile )
{
	if (file.is_open())
		file.close();

	file.open( outfile.c_str() );
	if (!file.is_open())
		cerr << "Can't open file: " << outfile << endl;

	m_filename = outfile;
}

void Processing::DumpBox2File( string vtkfilenameprefix, bool dualMesh ) const
{
	string vtkfilename = vtkfilenameprefix + m_filename + ".vtk";

	ofstream file( vtkfilename.c_str() );
	if (!file.is_open())
	{
		cerr << "Processing::DumpBoxes2File(): Can't open file: " << vtkfilename << endl;
		return;
	}

	// normalize coordinates
	double s1[3], s2[3];
	for (int i=0; i<3; i++)
	{
		s1[i] = min(Op->GetDiscLine(i,start[i],dualMesh),Op->GetDiscLine(i,stop[i],dualMesh));
		s2[i] = max(Op->GetDiscLine(i,start[i],dualMesh),Op->GetDiscLine(i,stop[i],dualMesh));
	}

	// fix degenerate box/plane -> line (paraview display problem)
	if (((s1[0] == s2[0]) && (s1[1] == s2[1])) || ((s1[0] == s2[0]) && (s1[2] == s2[2])) || ((s1[2] == s2[2]) && (s1[1] == s2[1])))
	{
		// line are not displayed correctly -> enlarge
		for (int i=0; i<3; i++)
		{
			double delta = min( Op->GetEdgeLength( i, start,dualMesh ), Op->GetEdgeLength( i, stop,dualMesh ) ) / Op->GetGridDelta() / 4.0;
			s1[i] -= delta;
			s2[i] += delta;
		}
	}

	// rescale coordinates
#ifndef OUTPUT_IN_DRAWINGUNITS
	double scaling = Op->GetGridDelta();
	for (int i=0; i<3; i++)
	{
		s1[i] *= scaling;
		s2[i] *= scaling;
	}
#endif

	file << "# vtk DataFile Version 2.0" << endl;
	file << "" << endl;
	file << "ASCII" << endl;
	file << "DATASET POLYDATA" << endl;

	file << "POINTS 8 float" << endl;
	file << s1[0] << " " << s1[1] << " " << s1[2] << endl;
	file << s2[0] << " " << s1[1] << " " << s1[2] << endl;
	file << s2[0] << " " << s2[1] << " " << s1[2] << endl;
	file << s1[0] << " " << s2[1] << " " << s1[2] << endl;
	file << s1[0] << " " << s1[1] << " " << s2[2] << endl;
	file << s2[0] << " " << s1[1] << " " << s2[2] << endl;
	file << s2[0] << " " << s2[1] << " " << s2[2] << endl;
	file << s1[0] << " " << s2[1] << " " << s2[2] << endl;

	file << "POLYGONS 6 30" << endl;
	file << "4 0 1 2 3" << endl;
	file << "4 4 5 6 7" << endl;
	file << "4 7 6 2 3" << endl;
	file << "4 4 5 1 0" << endl;
	file << "4 0 4 7 3" << endl;
	file << "4 5 6 2 1" << endl;

	file.close();
}

void Processing::Dump_FD_Data(vector<double_complex> value, double factor, string filename)
{
	if (value.size()==0)
		return;
	if (value.size()!=m_FD_Samples.size())
	{
		cerr << "Processing::Dump_FD_Data: Error: Complex value and frequency vector have different size! This should never happend!!!" << endl;
		return;
	}
	ofstream file;
	file.open( filename.c_str() );
	if (!file.is_open())
		cerr << "Can't open file: " << filename << endl;

	time_t rawTime;
	time(&rawTime);
	file << "%dump by openEMS @" << ctime(&rawTime) << "%frequency\treal\timag\n";
	for (size_t n=0; n<value.size(); ++n)
	{
		file << m_FD_Samples.at(n) << "\t" << 2.0 * std::real(value.at(n))*factor << "\t" << 2.0 * std::imag(value.at(n))*factor << "\n";
	}
	file.close();
}

void ProcessingArray::AddProcessing(Processing* proc)
{
	ProcessArray.push_back(proc);
}

void ProcessingArray::FlushNext()
{
	for (size_t i=0; i<ProcessArray.size(); ++i)
	{
		ProcessArray.at(i)->FlushNext();
	}
}

void ProcessingArray::Reset()
{
	for (size_t i=0; i<ProcessArray.size(); ++i)
	{
		ProcessArray.at(i)->Reset();
	}
}

void ProcessingArray::DeleteAll()
{
	for (size_t i=0; i<ProcessArray.size(); ++i)
	{
		delete ProcessArray.at(i);
	}
	ProcessArray.clear();
}

int ProcessingArray::Process()
{
	int nextProcess=maxInterval;
	//this could be done nicely in parallel??
	for (size_t i=0; i<ProcessArray.size(); ++i)
	{
		int step = ProcessArray.at(i)->Process();
		if ((step>0) && (step<nextProcess))
			nextProcess=step;
	}
	return nextProcess;
}

void ProcessingArray::DumpBoxes2File( string vtkfilenameprefix ) const
{
	for (size_t i=0; i<ProcessArray.size(); ++i)
		ProcessArray.at(i)->DumpBox2File( vtkfilenameprefix );
}
