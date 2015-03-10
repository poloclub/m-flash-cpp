/*
 * vector
 *
 *  Created on: Feb 28, 2015
 *      Author: hugo
 */

#ifndef MFLASH_CPP_CORE_VECTOR_HPP_
#define MFLASH_CPP_CORE_VECTOR_HPP_

#include <bits/vector.tcc>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "../log/easylogging++.h"

#include "array.hpp"
#include "linearcombination.hpp"
#include "operator.hpp"
#include "type.hpp"
#include "util.hpp"


using namespace std;

namespace mflash{

	template <class V>
	class Vector{
		protected:
			string file;
			int64 size;
			bool readonly;
			int64 elements_by_block;

			std::vector< OperationListener* > listeners;

			void invoke_operation_listener(int vector_id);

			static V operate(Operator<V> &operator_, Vector<V> &output,  int n, Vector<V>* vectors[]);

		public:
			Vector(string file, int64 size, int64 elements_by_block);
			int64 element_size(){ return sizeof(V);}
			//int64 get_size(){ return size;}
			string get_file(){return file;}
			/*static V operate(Operator<V> &operator_, Vector<V> &output, Vector<V> &v1);
			static V operate(Operator<V> &operator_, Vector<V> &output, Vector<V> &v1, Vector<V> &v2);*/

			int64 load_region(int64 offset, int64 size, V* address);
			void store_region(int64 offset, int64 size, V* address);

			void add_listener(OperationListener *listener );
			void remove_listener(OperationListener *listener );
			static void operate(BinaryOperator<V> &moperator, BinaryOperator<V> &soperator, Vector<V> &output, int n, V constants[], Vector<V> *vectors[]);
			void operate(ZeroOperator<V> &operator_);
			void operate(UnaryOperator<V> &operator_, Vector<V> &output);
			void operate(BinaryOperator<V> &operator_, Vector<V> &vector2, Vector<V> &output);
			V operate(UnaryReducer<V> &operator_);
			V operate(BinaryReducer<V> &operator_, Vector<V> &vector2);
	};



	template <class V>
	Vector<V>::Vector(string file, int64 size, int64 elements_by_block = 0){
		this->file = file;
		this->size = size;
		this->readonly = false;
		this->elements_by_block = (elements_by_block<=0?size: elements_by_block);

	}

	template <class V>
	V Vector<V>::operate(Operator<V> &operator_, Vector<V> &output, int n, Vector<V> *vectors[]){

		//int64 element_size = sizeof(V);
		int64 size = output.size;
		int64 elements_by_block = output.elements_by_block;
		int64 blocks = size / elements_by_block + ( size % elements_by_block == 0?0:1);

		Array<V>* out = new Array<V>(elements_by_block);
		Array<V>* tmp = new Array<V>(elements_by_block);


		if(n == 0){
				vectors = new Vector<V> *[1] {&output};
				n =1;//vectors[0] = output;
		}

		V final_accumulator;
		V tmp_accumulator;

		Reducer<V> *reducer = dynamic_cast<Reducer<V> *>(&operator_);
		bool default_reducer = false;

		if ( reducer == 0 ){
				//reducer = new DefaultReducer<V>();
				default_reducer = true;
		}else{
				reducer->initialize(final_accumulator);
		}

		int64 offset = 0;
		int64 block_size = 0;

		LOG (INFO) << "VECTOR OPERATION STARTED WITH " << blocks << " BLOCKS";
		for (int64 block = 0; block < blocks; block++) {
			LOG (INFO) << "PROCESSINGN BLOCK " << block;
			offset = block * elements_by_block;
			block_size = min(elements_by_block, size - offset);

			//loading the first vector on the output
			vectors[0]->load_region(offset, block_size, out->address());
			out->set_limit(block_size);
			out->set_offset(offset);

			int vector_indx = 0;

			BinaryOperator<V> *binary_operator = dynamic_cast<BinaryOperator<V> *>( &operator_ );
			/*
			 * When is a binary operator the output is considered the first value, then is not loaded twice
			 */
			if(binary_operator != 0){
				vector_indx = 1;
			}

			Vector<V> *v;
			do{
				output.invoke_operation_listener(vector_indx);

				v = vectors[vector_indx];
		//		cout << v->file <<endl;
		//		cout << output.file <<endl;
				if (v->file.compare(output.file) != 0) {
					/*if (v.inMemory) {
						tmpAccumulator = ThreadDataType.operate(operator, out, new Array<V>(type, v.address + offset, size, 0), out);
					} else {*/

						v->load_region(offset, block_size, tmp->address());
						tmp->set_offset(offset);
						tmp->set_limit(block_size);
						tmp_accumulator = Array<V>::operate(operator_, *out, *tmp, *out);
				//	}
				} else {
						tmp_accumulator = Array<V>::operate(operator_, *out, *out, *out);
					//tmpAccumulator = ThreadDataType.operate(operator, out, out, out);
				}
				if(!default_reducer){
						reducer->sum(final_accumulator, tmp_accumulator,final_accumulator);
				}

			}while( ++vector_indx < n);

			// store the output
			if (default_reducer) {
				//logger.debug("Storing block {} ", block);
				output.store_region(offset, block_size, out->address());
			}
		}

		delete out;
		delete tmp;

		LOG (INFO) << "VECTOR OPERATION FINALIZED";

		if (!default_reducer) {
			return final_accumulator;
		}
		return 0;
	}


	template <class V>
	void Vector<V>::operate(BinaryOperator<V> &moperator, BinaryOperator<V> &soperator, Vector<V> &output, int n, V constants[], Vector<V> *vectors[]){

		if (n < 2) throw 12; //error

		LinearOperator<V> loperator (constants, &soperator, &moperator);
		OperationListener * listener = &loperator;
		output.add_listener(listener);
		Vector<V>::operate(loperator, output, n,  vectors);
		output.remove_listener(listener);

	}

	template <class V>
	void Vector<V>::operate(ZeroOperator<V> &operator_){
		operate(operator_, *this, 0, new Vector<V> *[0]{});
	}

	template <class V>
	void Vector<V>::operate(UnaryOperator<V> &operator_, Vector<V> &output){
		operate(operator_, output, new Vector<V> *[1]{*this});
	}

	template <class V>
	void Vector<V>::operate(BinaryOperator<V> &operator_, Vector<V> &vector2, Vector<V> &output){
		operate(operator_, output, new Vector<V> *[2]{*this, &vector2});
	}

	template <class V>
	V Vector<V>::operate(UnaryReducer<V> &operator_){
		operate(operator_, *this, new Vector<V> *[0]{});
	}

	template <class V>
	V Vector<V>::operate(BinaryReducer<V> &operator_, Vector<V> &vector2){
		operate(operator_, *this, new Vector<V> *[2]{*this, &vector2});
	}


	template <class V>
	inline int64 Vector<V>::load_region(int64 offset, int64 size, V* address){
		int64 element_size = this->element_size();
		size = min(size, this->size - offset) * element_size;

		offset *= element_size;

		if(!exist_file(this->file)){
				return size/element_size;
		}

		ifstream *file = new ifstream(this->file, ios::in|ios::binary|ios::ate);
		if(file_size(this->file) >= offset+size){
			file->seekg (offset, ios::beg);
			file->read( (char*)address, size);
		}
		file->close();
/*
		FILE * pFile;
		pFile = fopen (this->file.c_str(), "r");
		fseek (pFile , 0 , SEEK_END);
		if(file_size(this->file) > offset+size){
			fseek(pFile, offset, SEEK_SET);
			fread(address, 1,size , pFile);
		}
		fclose(pFile);
*/

		return size/element_size;
	}

	template <class V>
	inline void Vector<V>::store_region(int64 offset, int64 size, V* address){
		int64 element_size = this->element_size();
		size = min(size, this->size - offset) * element_size;
		offset *= element_size;

		auto properties = ios::out|ios::binary|ios::ate;
		if(exist_file(this->file)){
				properties |= ios::in;
		}

		ofstream file (this->file,  properties);
		if(!file.good()){
				file.close();
				return;
		}
		file.seekp (offset, ios::beg);
		file.write((char*)address, size);
		file.close();
	}

	template <class V>
	void Vector<V>::add_listener(OperationListener *listener){
    if(listeners.end() != std::find(listeners.begin(), listeners.end(), listener) )
        return;
    listeners.push_back(listener);
	}

	template <class V>
	void Vector<V>::remove_listener(OperationListener *listener){
		std::vector<OperationListener*>::iterator iter = std::find(listeners.begin(), listeners.end(), listener);
		if( listeners.end() == iter )
				return;

		listeners.erase(iter);
	}

	template <class V>
	void Vector<V>::invoke_operation_listener(int vector_id){
		OperationEvent event(vector_id);
			for(std::vector<OperationListener*>::iterator iter = listeners.begin(); iter != listeners.end(); ++iter )
				 (*iter)->on_change(event);
	}
}
#endif /* MFLASH_CPP_CORE_VECTOR_HPP_ */
