#include "CompositeTransform.h"
#include "HunkList.h"
#include "Hunk.h"

using namespace std;

Hunk* CompositeTransform::getDetransformer() {
	if(m_transforms.empty())
		return NULL;

	HunkList hl;
	for(vector<Transform*>::iterator it = m_transforms.begin(); it != m_transforms.end(); it++) {
		hl.addHunkFront((*it)->getDetransformer());
	}
	Hunk* detrans = hl.toHunk("detransformers");
	return detrans;
}

void CompositeTransform::transform(Hunk* hunk, int splittingPoint) {
	for(vector<Transform*>::iterator it = m_transforms.begin(); it != m_transforms.end(); it++) {
		(*it)->transform(hunk, splittingPoint);		//TODO: master hack, do something here
	}
}

void CompositeTransform::addTransform(Transform* trans) {
	m_transforms.push_back(trans);
}